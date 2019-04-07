// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington UW TechTransfer, email: license@u.washington.edu.



#include <riflib/rif/RifGeneratorUserHotspots.hh>

    #include <basic/options/keys/in.OptionKeys.gen.hh>
	#include <basic/options/keys/mh.OptionKeys.gen.hh>
	#include <basic/options/keys/out.OptionKeys.gen.hh>
	#include <basic/options/keys/packing.OptionKeys.gen.hh>
	#include <basic/options/option_macros.hh>

	#include <ObjexxFCL/format.hh>

	#include <boost/random/mersenne_twister.hpp>
	#include <boost/random/uniform_real.hpp>

	#include <core/id/AtomID.hh>
	#include <core/pose/Pose.hh>
	#include <core/scoring/motif/util.hh>
	#include <core/import_pose/import_pose.hh>

  	#include <numeric/xyzMatrix.hh>
	#include <devel/init.hh>
	#include <riflib/RotamerGenerator.hh>
	#include <riflib/ScoreRotamerVsTarget.hh>
	#include <riflib/util.hh>
    #include <riflib/util_complex.hh>
	#include <scheme/numeric/rand_xform.hh>
	#include <scheme/actor/Atom.hh>
	#include <scheme/actor/BackboneActor.hh>

	#include <Eigen/SVD>
	#include <Eigen/Core>
	#include <Eigen/Geometry>

	#include <utility/io/ozstream.hh>



	#include <scheme/objective/hash/XformMap.hh>
	#include <scheme/objective/storage/RotamerScores.hh>
	#include <scheme/actor/BackboneActor.hh>
	#include <vector>
	#include <utility/vector1.hh>

    #include <riflib/rif/requirements_util.hh>



namespace devel {
namespace scheme {
namespace rif {

	void
	RifGeneratorUserHotspots::modify_rotamer_spec(
		::scheme::chemical::RotamerIndexSpec& rot_spec

	){
		for( int i_hotspot_group = 0; i_hotspot_group < this->opts.hotspot_files.size(); ++i_hotspot_group ){

			std::string const & hotspot_file = this->opts.hotspot_files[i_hotspot_group];
			core::pose::Pose pose;
			core::import_pose::pose_from_file(pose,hotspot_file);


			for( int i_hspot_res = 1; i_hspot_res <= pose.size(); ++i_hspot_res ){

				std::string resn;
				std::vector<float> mychi;
				int n_proton_chi;
				int parent_key;
				::scheme::chemical::get_residue_rotspec_params( pose.residue(i_hspot_res), resn, mychi, n_proton_chi, parent_key );

				int irot = rot_spec.get_matching_rot( resn, mychi, n_proton_chi, 5.0f );

				if ( irot == -1 ) {
					std::cout << "Adding input rotamers: " << i_hspot_res << " " << resn << std::endl;
					bool am_i_normal(pose.residue(i_hspot_res).has("N") && pose.residue(i_hspot_res).has("CA")  && pose.residue(i_hspot_res).has("C"));
					if (am_i_normal) {
						rot_spec.add_rotamer(resn,mychi,n_proton_chi,parent_key);
					}
				} else {
					std::cout << "duplicated rotamer, not adding: " << i_hotspot_group << " " << i_hspot_res << " " << resn << std::endl;
				}

			}//end loop over all hotspot res within one hotspot file
		}// end loop over all hotspot files
		//rot_spec.load();
		// utility::io::ozstream outfile("test_out.text");
		// rot_index_spec.save(outfile);
		// rot_index_spec.fill_rotamer_index(*(params->rot_index_p));
		std::cout << "modify_rotamer_sepc DONE " << std::endl;
	}// end modify_rotamer_spec


	void
	RifGeneratorUserHotspots::generate_rif(
		RifAccumulatorP accumulator,
		RifGenParamsP params
	){

		typedef ::scheme::actor::BackboneActor<EigenXform> BBActor;

		typedef ::Eigen::Matrix<float,3,1> Pos;

        using ObjexxFCL::format::I;

        // requirements definitions
        bool const use_requirement_definition = check_requirement_definition_exists( params->tuning_file );
        std::vector< int > hotspot_requirement_labels;
        if ( use_requirement_definition ) {
            std::vector< HotspotRequirement > hotspot_reqs = get_hotspot_requirement_definitions( params->tuning_file );
            // 20 is an arbitrary number, as I don't think there would be more than 20 hotspots.
            hotspot_requirement_labels.resize( 20 );
            for (int ii = 0; ii < hotspot_requirement_labels.size(); ++ii) {
                hotspot_requirement_labels[ii] = -1;
            }
            // fill the hotspot definitions
            for ( auto const & x : hotspot_reqs ) {
                hotspot_requirement_labels[ x.hotspot_num ] = x.req_num;
            }
            // for (auto i: hotspot_requirement_labels) {
            // 	std::cout << " print reqs: " << i << " " << std::endl;
            // }
        }


		// some sanity checks
		int const n_hspot_groups = this->opts.hotspot_files.size();
		runtime_assert_msg( n_hspot_groups, "no hotspot group files specified!!" );
		// runtime_assert_msg( n_hspot_groups<16, "too many hotspot groups!!" );

		std::cout << "this RIF type doesn't support sat groups!!!" << std::endl;

		std::cout << "RifGeneratorUserHotspots opts:" << std::endl;
		std::cout << "    hotspot_sample_cart_bound:  " << this->opts.hotspot_sample_cart_bound;
		std::cout << "    hotspot_sample_angle_bound: " << this->opts.hotspot_sample_angle_bound;
		std::cout << "    hbond_weight:               " << this->opts.hbond_weight;
		std::cout << "    upweight_multi_hbond:       " << this->opts.upweight_multi_hbond;
		std::cout << "    target_center:              "
			<< this->opts.target_center[0] << " "
			<< this->opts.target_center[1] << " "
			<< this->opts.target_center[2] << std::endl;
		//translation to apply to input hotspot files
		Pos target_vec;
		numeric::xyzVector<double> xyz_tgt_cen( this->opts.target_center[0], this->opts.target_center[1], this->opts.target_center[2] );
		target_vec << opts.target_center[0], opts.target_center[1],opts.target_center[2];


		for( auto s : this->opts.hotspot_files ){
			std::cout << "    hotspot_group:              " << s << std::endl;
		}



		// setup the hacky but fast scorer
		devel::scheme::ScoreRotamerVsTarget<
				VoxelArrayPtr, ::scheme::chemical::HBondRay, ::devel::scheme::RotamerIndex
			> rot_tgt_scorer;
		{
			std::vector< ::scheme::chemical::HBondRay > target_donors, target_acceptors;
			for( auto ir : params->target_res ){
				::devel::scheme::get_donor_rays   ( *params->target, ir, params->hbopt, target_donors );
				::devel::scheme::get_acceptor_rays( *params->target, ir, params->hbopt, target_acceptors );
			}
			std::cout << "target_donors.size() " << target_donors.size() << " target_acceptors.size() " << target_acceptors.size() << std::endl;


			{
				rot_tgt_scorer.rot_index_p_ = params->rot_index_p;
				rot_tgt_scorer.target_field_by_atype_ = params->field_by_atype;
				rot_tgt_scorer.target_donors_ = target_donors;
				rot_tgt_scorer.target_acceptors_ = target_acceptors;
				rot_tgt_scorer.hbond_weight_ = this->opts.hbond_weight;
				rot_tgt_scorer.upweight_multi_hbond_ = this->opts.upweight_multi_hbond;
				rot_tgt_scorer.upweight_iface_ = 1.0;
				rot_tgt_scorer.min_hb_quality_for_satisfaction_ = opts.min_hb_quality_for_satisfaction;
                rot_tgt_scorer.long_hbond_fudge_distance_ = opts.long_hbond_fudge_distance;
#ifdef USEGRIDSCORE
				rot_tgt_scorer.grid_scorer_ = params->grid_scorer;
				rot_tgt_scorer.soft_grid_energies_ = params->soft_grid_energies;
#endif
                shared_ptr<DonorAcceptorCache> target_donor_cache, target_acceptor_cache;
                prepare_donor_acceptor_cache( target_donors, target_acceptors, rot_tgt_scorer, target_donor_cache, target_acceptor_cache );

                rot_tgt_scorer.target_donor_cache_ = target_donor_cache;
                rot_tgt_scorer.target_acceptor_cache_ = target_acceptor_cache;
			}
		}




    	int const NSAMP = this->opts.hotspot_nsamples;
    	if (NSAMP > opts.dump_hotspot_samples && opts.dump_hotspot_samples > 0) utility_exit_with_message("too many NSAMP");


    	std::mt19937 rng((unsigned int)time(0) + 934875);
    	float const radius_bound = this->opts.hotspot_sample_cart_bound;
    	float const degrees_bound = this->opts.hotspot_sample_angle_bound;
    	float const radians_bound = degrees_bound * M_PI/180.0;


		std::ofstream hotspot_dump_file;
		std::ostringstream os;
		os << "hotspots.pdb";
		std::string s = os.str();
		if (opts.dump_hotspot_samples>=NSAMP){
			hotspot_dump_file.open (s, std::fstream::in | std::fstream::out | std::fstream::app);
			core::pose::Pose target_pose(*params->target);
			target_pose.dump_pdb(hotspot_dump_file);
		}

        uint64_t redundancy_new = 0;
        uint64_t redundancy_from_rif = 0;
        uint64_t redundancy_from_self = 0;

    	// std::ostream & out( std::cout );
    	// std::ofstream out;
    	// out.open("rifgen.txt");

		// loop over files (one file is one hotspot group)

        bool const single_thread = opts.test_hotspot_redundancy;
        bool const force_hotspot = opts.test_hotspot_redundancy | opts.label_hotspots_254;

        // We might be overriding later, so make sure everything is condensed now
        accumulator->checkpoint( std::cout, false );

		print_header("Building RIF from resampled user hotspots");
		// tallying hotspot stats
		std::map<std::tuple<int,int,std::string>, hotspot_stats> hstats;
		for( int i_hotspot_group = 0; i_hotspot_group < this->opts.hotspot_files.size(); ++i_hotspot_group ){



			std::string const & hotspot_file = this->opts.hotspot_files[i_hotspot_group];
			std::cout << "Hotspot group: " << i_hotspot_group << " - " << hotspot_file << std::endl;



			// read hotspot file into pose
			core::pose::Pose pose;
			core::import_pose::pose_from_file(pose,hotspot_file);

			utility::vector1<std::vector<std::string>>myresname(pose.size());
			utility::vector1<std::vector<std::string>>d_name(pose.size());
			utility::vector1<bool> ftmap_(2,false); // only support BNZ and PHN from ftmap
			int original_pose_size = pose.size();
			for (int i = 1; i <= original_pose_size; ++i) {
                std::string rn(pose.residue(i).name3());
                // only use for ftmap stuff
                core::pose::PoseOP tmp = pose.clone();
                core::conformation::ResidueOP res = tmp -> residue(1).clone();

				if (rn == "CA_") { // carboxamide
					myresname[i].push_back("GLN");
					myresname[i].push_back("ASN");
					d_name[i].push_back(static_cast<std::string>("DUMMY"));
					d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "OH_") { // hydroxyl
                    myresname[i].push_back("TYR");
                    myresname[i].push_back("SER");
                    myresname[i].push_back("THR");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "G__") { // guanidium
                    myresname[i].push_back("GLN");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "I__") { // imidazole
                    myresname[i].push_back("HIS");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "ID_") { //imidazole_D
                    // does this work?
                    myresname[i].push_back("HIS_D");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "A__") { // amine
                    myresname[i].push_back("LYS");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "C__") { // carboxylate
                    myresname[i].push_back("GLU");
                    myresname[i].push_back("ASP");
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                    d_name[i].push_back(static_cast<std::string>("DUMMY"));
                } else if (rn == "BNZ") {
                	//need to overlap 6 different PHE alignement
                	myresname.resize(6);
                	d_name.resize(6);
                	pose.append_residue_by_jump(*res,i);
                	myresname[i].push_back("PHE");
                	d_name[i].push_back(static_cast<std::string>("DUMMY"));
                	for (int j=1; j < 6; j++){
                		pose.append_residue_by_jump(*res,i+j);
                		myresname[i+j].push_back("PHE");
                		d_name[i+j].push_back(static_cast<std::string>("DUMMY"));
                	}
                	ftmap_[1] = true;       	
                } else if (rn == "PHN") {
                	myresname[i].push_back("TYR");
                	d_name[i].push_back(static_cast<std::string>("DUMMY"));
                	ftmap_[2] = true;
            	} else {
					myresname[i].push_back(pose.residue(i).name3());
					d_name[i].push_back(static_cast<std::string>("DUMMY"));
					std::cout << "adding first" << pose.residue(i).name3() << std::endl;
					if (this->opts.use_d_aa) {
						for (auto it : params -> rot_index_p -> d_l_map_){
							if (it.second == rn){
								std::replace(d_name[i].begin(),d_name[i].end(),static_cast<std::string>("DUMMY"),it.first);
							}
						}
					}
				}
			}
			for (int num_res = 1; num_res <= myresname.size(); num_res++) {
				for (int res = 0; res < myresname[num_res].size(); res++) {
					std::cout << "Adding: " << i_hotspot_group << " : " << num_res << " : " << myresname[num_res][res] << std::endl;
					hstats.insert(std::pair<std::tuple<int, int, std::string>,hotspot_stats>(std::make_tuple(i_hotspot_group, num_res, myresname[num_res][res]),hotspot_stats(i_hotspot_group, num_res, myresname[num_res][res])));
				}
			}

      		std::cout << "Processing hotspots... " << std::flush; // No endl here!!!!
			// read in pdb files # i_hotspot_group

			for( int i_hspot_res = 1; i_hspot_res <= myresname.size(); ++i_hspot_res ){

			  //   std::vector<std::string> d_name(myresname.size(),"NONE");
			  //   // try to find matching d version if use_d_aa
 				// if (this->opts.use_d_aa) {
	    //             // loop thorugh the d_l_map_
	    //             for (auto it : params -> rot_index_p -> d_l_map_) {
	    //             	// loop through the added hotspot
	    //             	for (auto const & name_it: myresname[i_hspot_res]){
	    //                 	//std::cout << it.second << " " << name_it << std::endl;
	    //                 	if (it.second == name_it){//pose.residue(i_hspot_res).name3()) {
	    //                 		d_name.push_back(it.first);
	    //                 		//d_name.push_back(" ");
	    //                 	}
	    //             	}
	    //             }
	    //         }

            	// if (this->opts.use_d_aa && d_name == " ") {
             //   		std::cout << pose.residue(i_hspot_res).name3() << std::endl;
             //   		utility_exit_with_message("can't find d version");
            	// } else {
            	// 	std::cout << d_name << std::endl;
            	// }

				std::cout << i_hspot_res << " " << std::flush; // No endl here!!!!
				// possible name still OK
				int input_nheavy = pose.residue(i_hspot_res).nheavyatoms();
                if (input_nheavy < 3) { // this can happen for disembodied hydroxyls
                    input_nheavy = 3;
                }
                EigenXform Xref;
                Pos hot_cen;
               	//these are the hotspot atoms				
				Pos hot_atom1; Pos hot_atom2; Pos hot_atom3;
                if (ftmap_[2]) {
                	// last three stub into Xform for transform calculation
                	Xref = ::scheme::chemical::make_stub<EigenXform>(
		            pose.residue(i_hspot_res).atom("C5").xyz() - xyz_tgt_cen,
		            pose.residue(i_hspot_res).atom("C6").xyz() - xyz_tgt_cen,
		            pose.residue(i_hspot_res).atom("O1").xyz() - xyz_tgt_cen
		        	);
                	// loop to get xyz of last three atom
					for (int i = 0; i < 3; ++ i) {
						hot_atom1(i,0) = pose.residue(i_hspot_res).atom("C5").xyz()[i];
						hot_atom2(i,0) = pose.residue(i_hspot_res).atom("C6").xyz()[i];
						hot_atom3(i,0) = pose.residue(i_hspot_res).atom("O1").xyz()[i];
					}
                } 
                if (ftmap_[1]) {
                	if (i_hspot_res > 1 && i_hspot_res < 6) {
                		Xref = ::scheme::chemical::make_stub<EigenXform>(
		            	pose.residue(i_hspot_res).xyz(i_hspot_res - 1) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(i_hspot_res + 1) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(i_hspot_res	 ) - xyz_tgt_cen
		        		);
						// loop to get xyz of last three atom
						for (int i = 0; i < 3; ++ i) {
							hot_atom1(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res - 1)[i];
							hot_atom2(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res + 1)[i];
							hot_atom3(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res    )[i];
						}
                	} else if (i_hspot_res == 1) {
                		Xref = ::scheme::chemical::make_stub<EigenXform>(
		            	pose.residue(i_hspot_res).xyz(6) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(i_hspot_res + 1) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(i_hspot_res) - xyz_tgt_cen
		        		);
						// loop to get xyz of last three atom
						for (int i = 0; i < 3; ++ i) {
							hot_atom1(i,0) = pose.residue(i_hspot_res).xyz(6)[i];
							hot_atom2(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res + 1)[i];
							hot_atom3(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res)[i];
						}
                	} else if (i_hspot_res == 6) {
                		Xref = ::scheme::chemical::make_stub<EigenXform>(
		            	pose.residue(i_hspot_res).xyz(i_hspot_res - 1) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(1) - xyz_tgt_cen,
		            	pose.residue(i_hspot_res).xyz(i_hspot_res) - xyz_tgt_cen
		        		);
						// loop to get xyz of last three atom
						for (int i = 0; i < 3; ++ i) {
							hot_atom1(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res - 1)[i];
							hot_atom2(i,0) = pose.residue(i_hspot_res).xyz(1)[i];
							hot_atom3(i,0) = pose.residue(i_hspot_res).xyz(i_hspot_res)[i];
						}
                	}
            	} 
            	if ( !ftmap_[1] && !ftmap_[2]){
                	Xref = ::scheme::chemical::make_stub<EigenXform>(
		            pose.residue(i_hspot_res).xyz( input_nheavy - 2 ) - xyz_tgt_cen,
		            pose.residue(i_hspot_res).xyz( input_nheavy - 1 ) - xyz_tgt_cen,
		            pose.residue(i_hspot_res).xyz( input_nheavy - 0 ) - xyz_tgt_cen
		        	);
		        	//get last atom in hotspot residue and iterate over last 3 
      				core::conformation::Atoms::const_iterator iter = pose.residue(i_hspot_res).heavyAtoms_end();
					core::conformation::Atoms::const_iterator end = iter-3;
					while(iter >= end){
        				if (iter == end +2){hot_atom1(0,0) = iter->xyz()[0];hot_atom1(1,0) = iter->xyz()[1];hot_atom1(2,0) = iter->xyz()[2];}
        				if (iter == end +1){hot_atom2(0,0) = iter->xyz()[0];hot_atom2(1,0) = iter->xyz()[1];hot_atom2(2,0) = iter->xyz()[2];}
        				if (iter == end +0){hot_atom3(0,0) = iter->xyz()[0];hot_atom3(1,0) = iter->xyz()[1];hot_atom3(2,0) = iter->xyz()[2];}
        				iter --;
      				}
                }
      			// move with the target
      			hot_atom1 = hot_atom1 - target_vec; 
				hot_atom2 = hot_atom2 - target_vec; 
				hot_atom3 = hot_atom3 - target_vec;	
				//calculate centroid of hot_spot res and translate with target
				hot_cen = (hot_atom1 + hot_atom2 + hot_atom3)/3;
				// for each irot that is the right restype (can be had from rot_intex_p)
				int irot_begin = 0, irot_end = params -> rot_index_p -> size();
				std::cout << irot_begin << "   " << irot_end << std::endl; 
				for( int irot = irot_begin; irot < irot_end; ++irot ){
					::Eigen::Matrix<float,3,3> rif_res; // this is the rif residue last three atoms matrix

					// assign rif_res position by column
					int hatoms = params -> rot_index_p -> nheavyatoms(irot);

					//for (auto const & it: myresname[i_hspot_res]){
					for (int i = 0; i < myresname[i_hspot_res].size(); i++) {
						auto it = myresname[i_hspot_res][i];
						auto d_it =  d_name[i_hspot_res][i];
						if (params -> rot_index_p -> resname(irot) == it || params -> rot_index_p -> resname(irot) == d_it)
						{
							std::vector<SchemeAtom> const & rotamer_atoms( params->rot_index_p->atoms(irot) );
							EigenXform Xrotamer = ::scheme::chemical::make_stub<EigenXform>(
		                        rotamer_atoms.at( params->rot_index_p->nheavyatoms(irot) - 3 ).position(),
		                        rotamer_atoms.at( params->rot_index_p->nheavyatoms(irot) - 2 ).position(),
		                        rotamer_atoms.at( params->rot_index_p->nheavyatoms(irot) - 1 ).position()
		                    );
                            // TODO: Swap out the last three heavy atoms for the appropriate atoms when superposing on a disembodied hydroxyl
                            // Getting this right is going to be a little tricky -- all of the following is based on aligning to  OH_ (which needs to be checked)
                            if (pose.residue(i_hspot_res).name3() == "OH_"){
                                // For TYR: the last two heavy atoms and 'HH' -- atom number 20
                                // For SER: the last two heavy atoms and 'HG'
                                // For THR: the last two heavy atoms and 'HG1'
                                // both are atom number 10.
                                int atmno = (params -> rot_index_p -> resname(irot) == "TYR") ? 20 : 10;
                                Xrotamer = ::scheme::chemical::make_stub<EigenXform>(
                                    rotamer_atoms.at( params->rot_index_p->nheavyatoms(irot) - 2 ).position(),
                                    rotamer_atoms.at( params->rot_index_p->nheavyatoms(irot) - 1 ).position(),
                                    rotamer_atoms.at( atmno ).position()
                                );
                            }

							EigenXform impose; //transform for mapping the Rot to Rif
							::Eigen::Matrix<float,3,3> rif_res; // this is the rif residue last three atoms
							int latoms = params -> rot_index_p -> natoms(irot);
							rif_res << rotamer_atoms[hatoms-1].position()[0],rotamer_atoms[hatoms-2].position()[0],rotamer_atoms[hatoms-3].position()[0],rotamer_atoms[hatoms-1].position()[1],rotamer_atoms[hatoms-2].position()[1],rotamer_atoms[hatoms-3].position()[1],rotamer_atoms[hatoms-1].position()[2],rotamer_atoms[hatoms-2].position()[2],rotamer_atoms[hatoms-3].position()[2];
		     	 			Pos rot_cen = (rif_res.col(0) + rif_res.col(1) + rif_res.col(2))/3;


							impose = Xref * Xrotamer.inverse();
							//Additional matrix definition for manipulation
							//Default Rot starting Xform
							EigenXform x_orig_position = EigenXform::Identity();
							//Xform to move rif to and from starting postion
							EigenXform x_2_orig = EigenXform::Identity();
		     				x_2_orig.translation() = -hot_cen;
		     				EigenXform x_2_orig_inverse = x_2_orig.inverse();

							int passes = 1;
							EigenXform O_2_orig = EigenXform::Identity();
		     				EigenXform tyr_thing = EigenXform::Identity();
							if (pose.residue(i_hspot_res).name3() == "TYR" || d_it == "DTY") {

								Pos the_axis = (hot_atom1 - hot_atom2).normalized();
								O_2_orig.translation() = -hot_atom1;
								tyr_thing.rotate( Eigen::AngleAxisf(M_PI, the_axis));

								passes = 2;

							} else if (pose.residue(i_hspot_res).name3() == "PHE" ||  d_it == "DPH" ) {
								
								Pos atom6;
								atom6(0,0) = pose.residue(i_hspot_res).xyz( input_nheavy - 5 )[0];
								atom6(1,0) = pose.residue(i_hspot_res).xyz( input_nheavy - 5 )[1];
								atom6(2,0) = pose.residue(i_hspot_res).xyz( input_nheavy - 5 )[2];
								atom6 = atom6 - target_vec;

								Pos the_axis = (hot_atom1 - atom6).normalized();
								O_2_orig.translation() = -hot_atom1;
								tyr_thing.rotate( Eigen::AngleAxisf(M_PI, the_axis));

								passes = 2;
							}


							EigenXform O_2_orig_inverse = O_2_orig.inverse();



							for ( int pass = 0; pass < passes; pass++) {
								int num_of_hotspot_inserted = 0;

                                if ( single_thread ) { //single_thread
                                    omp_set_num_threads(1);
                                }

								#ifdef USE_OPENMP
								#pragma omp parallel for schedule(dynamic,16)
								#endif

								for(int a = 0; a < NSAMP; ++a){
									EigenXform x_perturb;
									::scheme::numeric::rand_xform_sphere(rng,x_perturb,radius_bound,radians_bound);

									EigenXform building_x_position = impose * x_orig_position;
									if ( pass == 1 ) {
										building_x_position = O_2_orig_inverse * tyr_thing * O_2_orig * building_x_position;
									}

									EigenXform x_position = x_2_orig_inverse * x_perturb * x_2_orig * building_x_position;
									//EigenXform x_position = x_2_orig_inverse * x_2_orig * building_x_position;

									// you can check their "energies" against the target like this, obviously substituting the real rot# and position
                                    int actual_sat1=-1, actual_sat2=-1, hbcount=0;
									float positioned_rotamer_score = rot_tgt_scorer.score_rotamer_v_target_sat( irot, x_position,
                                            actual_sat1, actual_sat2, true, hbcount, 10.0, 0 );

                                    if ( opts.all_hotspots_are_bidentate && ( actual_sat1 == -1 || actual_sat2 == -1 ) ) continue;


									if( positioned_rotamer_score < opts.hotspot_score_thresh ){ // probably want this threshold to be an option or something

										//num_of_hotspot_inserted += 1;
										// EigenXform x_position = x_2_orig_inverse * x_perturb  * x_2_orig * impose * x_orig_position; //test
									    // you can check their "energies" against the target like this, obviously substituting the real rot# and position
										//float positioned_rotamer_score = rot_tgt_scorer.score_rotamer_v_target( irot, x_position );


										//target_pose.dump_pdb(s);
										//myfile.open (s, std::fstream::in | std::fstream::out | std::fstream::app);
										//if( positioned_rotamer_score > 0) {positioned_rotamer_score = -1;}

										//std::cout << positioned_rotamer_score << std::endl;
										//accumulator->insert( x_position, positioned_rotamer_score-100, irot, i_hotspot_group, -1 );


										auto atom_N = x_position * rotamer_atoms[0].position();
										auto atom_CA = x_position * rotamer_atoms[1].position();
										auto atom_C = x_position * rotamer_atoms[2].position();

										BBActor bbact( atom_N, atom_CA, atom_C);
										EigenXform new_x_position = bbact.position();

										#pragma omp critical
										{
											std::map<std::tuple<int,int,std::string>, hotspot_stats>::iterator it;
											//it = hstats.find(std::make_tuple(0,1,std::string("PHE")));
											//std::cout << "try to find this: " << i_hotspot_group << " " << i_hspot_res << " " << params -> rot_index_p -> resname(irot) << std::endl;
											it = hstats.find(std::make_tuple(i_hotspot_group, i_hspot_res, params -> rot_index_p -> resname(irot)));
											if (it != hstats.end()) {
												it -> second.scores.push_back(positioned_rotamer_score);
												it -> second.bb_pos.push_back(new_x_position);
											}
										}

                                        int sat1 = this -> opts.single_file_hotspots_insertion ? i_hspot_res : i_hotspot_group;
                                        int sat2 =-1;
                                        if ( use_requirement_definition ) {
                                            // as the numbering of i_hotspot_group starts from 0.
                                            sat1 = hotspot_requirement_labels[ i_hotspot_group + 1 ];
                                        }

                                        if ( opts.test_hotspot_redundancy ) {

                                            std::set<size_t> in_rif = accumulator->get_sats_of_this_irot( new_x_position, irot );

                                            bool is_us = in_rif.count(254) > 0;
                                            bool anything = in_rif.size() != 0;

                                            if ( is_us ) {
                                                redundancy_from_self++;
                                            } else{
                                                if ( anything ) {
                                                    redundancy_from_rif++;
                                                } else {
                                                    redundancy_new++;
                                                }
                                            }

                                            positioned_rotamer_score = -20.0f;
                                            sat1 = 254;


                                        }

										if ( opts.label_hotspots_254 ) {
											sat1 = 254;
										}
										//std::cout << "inserting: " << sat1 << std::flush;
                                        //accumulator->insert( new_x_position, positioned_rotamer_score + opts.hotspot_score_bonus, irot, sat1, sat2, force_hotspot, single_thread);
                                        accumulator->insert( new_x_position, positioned_rotamer_score - 10, irot, sat1, sat2, force_hotspot, single_thread);


									 	if (opts.dump_hotspot_samples>=NSAMP){
									 		hotspot_dump_file <<"MODEL        "<<irot<<a<<"                                                                  \n";
											for( auto a : rotamer_atoms ){
											 	a.set_position( x_position * a.position() );
											 	::scheme::actor::write_pdb(hotspot_dump_file, a, params->rot_index_p->chem_index_ );
											}
											hotspot_dump_file <<"ENDMDL                                                                          \n";
										} //end dumping hotspot atoms



										// myfile.close();
									} // end rotamer insertion score cutoff

								} // end NSAMP
								//std::cout << "this is how many inserted: " << i_hotspot_group << " " << irot << " " << num_of_hotspot_inserted << std::endl;

							} // end brian ring flip

						} // end loop over rotamers which match hotspot res name
					} // loop over vector of input hotspot names

				} //  end loop over rotamers library

			} // end loop over hotspot group residue (with in one input pdb)

		}// end loop over all hotspot input files

		std::cout << std::endl; // This ends the "progress bar"

		// if (opts.dump_hotspot_samples>=NSAMP){
		// 	hotspot_dump_file.close();
		// }
		if (true) {
			//std::ofstream hotspot_sc, rif_sc, seed_pos;
			std::ofstream seed_pos;
  			// hotspot_sc.open ("hotspot_score.sc");
  			// rif_sc.open("rif_score.sc");
  			seed_pos.open("seeding_list");
  			auto rif_ptr = accumulator->rif();
			for (std::map<std::tuple<int,int,std::string>, hotspot_stats>::iterator it = hstats.begin(); it!=hstats.end(); ++it) {
				//scorefile << std::get<0>(it -> first) <<  " "  << std::get<1>(it -> first) << " " << std::get<2>(it -> first) << std::endl;
				//float insert_avg = std::accumulate( it -> second.scores.begin(), it -> second.scores.end(), 0.0) / it -> second.scores.size();
				//std::cout << "avg score: " << insert_avg << std::endl; 
				// for (auto score : it -> second.scores) {
				// 	hotspot_sc << score << " ";
					
				// }
				std::vector<std::string> bin_centers;
				for (auto trans : it -> second.bb_pos) {
					// std::cout << trans.linear().row(0) << std::endl;
					// std::cout << trans.linear().row(1) << std::endl;
					// std::cout << trans.linear().row(2) << std::endl;
					// std::cout << trans.translation() << std::endl;
					// std::cout << "end insert trans" << std::endl;
					//seed_pos << trans.linear().row(0) << " " << trans.linear().row(1) << " " << trans.linear().row(2) << " " << trans.translation() << std::endl;
					uint64_t const key = rif_ptr -> get_bin_key( trans );
					EigenXform bin_cen = rif_ptr -> get_bin_center(key);
					// std::string test_string;
					// test_string << bin_cen.linear().row(0) << ' ' << bin_cen.linear().row(1);
					// for (int a = 0; a < 3; ++a) {
					// 	test_string += static_cast<float>(bin_cen(a,0));
					// 	// test_string += " ";
					// 	// test_string += bin_cen(a,1);
					// 	// test_string += " ";
					// 	// test_string += bin_cen(a,2);
					// 	// test_string += " ";
					// }
					// std::cout << test_string << std::endl;
					// for (int a = 0; a < 3; ++a) {
					// 	test_string += bin_cen(a,4);
					// 	test_string += " ";
					// }
					//std::cout << test_string << std::endl;	
					// if (std::find(bin_centers.begin(), bin_centers.end(), test_string) == bin_centers.end()) {
					// 	bin_centers.push_back(test_string);

					// } 
					// for (auto bin : bin_centers) {
					// 	bool eq = bin_cen.isApprox(bin,10);
					// 	if (eq) {
					// 		bin_centers.push_back(bin_cen);
					// 	} else if (!eq) {
					// 		continue;
					// 	}
					// }


					//std::vector<std::pair<EigenXform,float>> get_rif_hot = accumulator->get_scores_of_this_irot_bbpos( it, params -> rot_index_p -> index_bounds(std::get<2>(it -> first)) );
					// for (auto j : get_rif_hot -> first) {
					// std::cout << bin_cen.linear().row(0) << std::endl;
					// std::cout << bin_cen.linear().row(1) << std::endl;
					// std::cout << bin_cen.linear().row(2) << std::endl;
					// std::cout << bin_cen.translation() << std::endl;
					// std::cout << "end center trans" << std::endl;

					seed_pos << bin_cen.linear().row(0) << ' ' << bin_cen.linear().row(1) <<  ' ' << bin_cen.linear().row(2) << ' ' << bin_cen.translation()[0] << ' ' <<bin_cen.translation()[1] << ' ' << bin_cen.translation()[2] << std::endl;
					// }
				}
				// std::cout << "total size of bins: " << bin_centers.size() << std::endl;
				// hotspot_sc << std::endl;
				// std::cout << params -> rot_index_p -> index_bounds(std::get<2>(it -> first)).first << ":" <<  params -> rot_index_p -> index_bounds(std::get<2>(it -> first)).second << std::endl;
				// //scorefile << "existing rif:  " << std::endl;
				// for (auto bb : it -> second.bb_pos) {
				// 	std::vector<std::pair<EigenXform,float>> get_rif = accumulator->get_scores_of_this_irot_bbpos( bb, params -> rot_index_p -> index_bounds(std::get<2>(it -> first)) );
					
				// 	if (!get_rif.empty()) {
				// 		//scorefile << "existing rif:  " << std::endl;
				// 		for (auto rif_score : get_rif) {
				// 			rif_sc << rif_score.second << " ";
				// 			//std::cout << rif_score.first.linear().row(0) << std::endl;
				// 		}
				// 		rif_sc << std::endl;
				// 		//float rif_avg = std::accumulate( get_rif.begin(), get_rif.end(), 0.0) / get_rif.size();
				// 		//std::cout << "avg rif score: " << rif_avg << std::endl; 
				// 	}
				// }
			}
			// hotspot_sc.close();
			// rif_sc.close();
			seed_pos.close(); 
		}
		// let the rif builder thing know you're done
		accumulator->checkpoint( std::cout, force_hotspot );



        if ( opts.test_hotspot_redundancy ) {

            std::cout << "================= Redundancy report =====================" << std::endl;
            std::cout << "            New: " << I(8, redundancy_new) << std::endl;
            std::cout << "  Rif redundant: " << I(8, redundancy_from_rif) << std::endl;
            std::cout << " Self redundant: " << I(8, redundancy_from_self) << std::endl;

        }

	} //end RifGeneratorUserHotspots

		//auto rif_ptr = accumulator->rif();
        // std::cout << "testing rifbase key iteration" << std::endl;
        // int count = 0;
        //for( auto key : rif_ptr->key_range() ){
        //     EigenXform bin_center = rif_ptr->get_bin_center(key);
        //     right... the edges of the bins.... this is only *mostly* true
        //     runtime_assert( rif_ptr->get_bin_key(bin_center) == key );
        //     std::cout << "BIN: key " << key << " xform.trans: " << bin_center.translation().transpose() << std::endl;
        //     auto rotscores = rif_ptr->get_rotamers_for_key(key);
        //     runtime_assert( rotscores.size() > 0 );
        //     for( auto rot_score : rotscores ){
        //         // can't wait for cxx17 structured bindings!!!
        //         float rotamer_score = rot_score.first;
        //         int rotamer_number = rot_score.second;
        //         std::string resn = params->rot_index_p->resname(rotamer_number);
        //         std::cout << " rotamer " << rotamer_number << " " << resn << ", score " << rotamer_score << std::endl;
        //     }

        //     //if(++count > 10) utility_exit_with_message("aireost");
        // }


		//auto rif_ptr = accumulator->rif();
		//std::cout << "Brian" << std::endl;
}
}
}


