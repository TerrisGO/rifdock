// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington UW TechTransfer, email: license@u.washington.edu.



#ifndef INCLUDED_riflib_rif_RifGenerator_hh
#define INCLUDED_riflib_rif_RifGenerator_hh

#include <riflib/types.hh>
#include <riflib/RotamerGenerator.hh>
#include <riflib/RifBase.hh>
#include <core/pose/Pose.hh>
#include <utility/vector1.hh>
#include <string>

#ifdef USEGRIDSCORE
	#include <protocols/ligand_docking/GALigandDock/GridScorer.hh>
#endif

namespace scheme {
namespace chemical {
	struct RotamerIndexSpec;
}
}

namespace devel {
namespace scheme {
namespace rif {

using ::devel::scheme::shared_ptr;

struct RifAccumulator {
	virtual ~RifAccumulator(){}
	virtual void insert( EigenXform const & x, float score, int rot, int sat1=-1, int sat2=-1, bool force=false, bool single_thread=false) = 0;
	virtual void report( std::ostream & out ) const = 0;
	virtual void checkpoint( std::ostream & out, bool force_override=false ) = 0;
	virtual uint64_t n_motifs_found() const = 0;
	virtual int64_t total_samples() const = 0;
	virtual void condense(bool force_override=false) = 0;
	virtual bool need_to_condense() const = 0;
	virtual shared_ptr<RifBase> rif() const = 0;
	virtual void clear() = 0; // seems to only clear temporary storage....
	virtual uint64_t count_these_irots( int irot_low, int irot_high ) const = 0;
	virtual std::set<size_t> get_sats_of_this_irot( devel::scheme::EigenXform const & x, int irot ) const = 0;
	virtual std::vector<std::pair<EigenXform,float>> get_scores_of_this_irot_bbpos( devel::scheme::EigenXform const & x, std::pair<int,int> bound) const = 0;
};
typedef shared_ptr<RifAccumulator> RifAccumulatorP;

struct RifGenParams {
  	core::pose::PoseOP             target = nullptr;
	std::string                    target_tag;
	std::string                    output_prefix="./default_";
	// set the fine-tuning file here, so it can control the rifgen processes of polar, apolar and hotspots.
  std::string																			 tuning_file="";
	utility::vector1<int>          target_res;
	//shared_ptr<RotamerIndex const> rot_index_p = nullptr;
	shared_ptr<RotamerIndex> rot_index_p = nullptr;
	std::vector<std::string>       cache_data_path;
	std::vector< VoxelArray* >     field_by_atype;
	HBRayOpts                      hbopt;
#ifdef USEGRIDSCORE
	shared_ptr<protocols::ligand_docking::ga_ligand_dock::GridScorer> grid_scorer;
	bool                           soft_grid_energies;
#endif
};
typedef shared_ptr<RifGenParams> RifGenParamsP;


struct RifGenerator {
	virtual ~RifGenerator(){}
	virtual void generate_rif(
		RifAccumulatorP accumulator,
		RifGenParamsP params
	) = 0;
	virtual void modify_rotamer_spec(::scheme::chemical::RotamerIndexSpec& rot_spec){}
};


}
}
}

#endif
