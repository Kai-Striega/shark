//
// ICRAR - International Centre for Radio Astronomy Research
// (c) UWA - The University of Western Australia, 2017
// Copyright by UWA (in the framework of the ICRAR)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

/**
 * @file
 */

#include <cmath>
#include <memory>

#include "components.h"
#include "evolve_halos.h"
#include "logging.h"
#include "numerical_constants.h"

using namespace std;

namespace shark {

void adjust_main_galaxy(const SubhaloPtr &parent, const SubhaloPtr &descendant)
{
	// A subhalo that is not main progenitor of its descendant cannot
	// contribute its central galaxy (CENTRAL or TYPE1, depending on the
	// subhalo's type) as the central galaxy of the descendant.

	auto parent_is_central = parent->subhalo_type == Subhalo::CENTRAL;
	auto desc_is_central = descendant->subhalo_type == Subhalo::CENTRAL;
	auto is_main_progenitor = parent->main_progenitor;
	auto main_galaxy = (parent_is_central ? parent->central_galaxy() : parent->type1_galaxy());

	if (!main_galaxy) {
		return;
	}
	if (desc_is_central) {
		main_galaxy->galaxy_type = (is_main_progenitor ? Galaxy::CENTRAL : Galaxy::TYPE2);
	}
	else {
		main_galaxy->galaxy_type = (is_main_progenitor ? Galaxy::TYPE1 : Galaxy::TYPE2);
	}

	// If main_galaxy is type 2, then define subhalo properties of types 2.
	if (main_galaxy->galaxy_type == Galaxy::TYPE2) {
		main_galaxy->concentration_type2 = parent->concentration;
		main_galaxy->msubhalo_type2 = parent->Mvir;
		main_galaxy->lambda_type2 = parent->lambda;
	}

}

void transfer_galaxies_to_next_snapshot(const std::vector<HaloPtr> &halos, int snapshot, TotalBaryon &AllBaryons)
{
	unsigned int subhalos_without_descendant = 0;
	double baryon_mass_loss = 0;

	// Make sure descendants are completely empty
	for(auto &halo: halos){
		for(auto &subhalo: halo->all_subhalos()) {
			if (!subhalo->descendant) {
				continue;
			}
			assert(subhalo->descendant->galaxy_count() == 0);
		}
	}

	for(auto &halo: halos){
		for(auto &subhalo: halo->all_subhalos()) {

			// Make sure all SFRs (in mass and metals) are set to 0 for the next snapshot
			for (GalaxyPtr & galaxy: subhalo->galaxies){
				galaxy->sfr_bulge_mergers  = 0;
				galaxy->sfr_z_bulge_mergers= 0;
				galaxy->sfr_bulge_diskins  = 0;
				galaxy->sfr_z_bulge_diskins= 0;
				galaxy->sfr_z_disk         = 0;
				galaxy->sfr_disk           = 0;
				//restart counter of mergers and disk instabilities.
				galaxy->interaction.restore_interaction_item();
			}

			// Check if this is a satellite subhalo, and whether this is the last snapshot in which it is identified.
			// In that case, the transfer of galaxies has already been done in merging_subhalos.
			// In any other case, we need to do the transfer.
			if(subhalo->subhalo_type == Subhalo::SATELLITE && subhalo->last_snapshot_identified == subhalo->snapshot) {
				continue;
			}

			auto descendant_subhalo = subhalo->descendant;

			if (!descendant_subhalo) {
				subhalos_without_descendant++;
				baryon_mass_loss += subhalo->total_baryon_mass();
				continue;
			}

			if(subhalo->snapshot != descendant_subhalo->snapshot-1){
				std::ostringstream os;
				os << "Descendant subhalo is not in the subsequent snapshot";
				throw invalid_argument(os.str());
			}

			// Perform the transfer of galaxies
			// We check that the subhalo and the descendant have a proper
			// galaxy composition before and after the transfer of galaxies
			// The transfer itself consists on adjusting the type of the main
			// galaxy of this subhalo and then transfer ownership of galaxies
			// over to the descendant
			subhalo->check_subhalo_galaxy_composition();
			adjust_main_galaxy(subhalo, descendant_subhalo);
			subhalo->transfer_galaxies_to(descendant_subhalo);

			// Transfer subhalo baryon components.
			descendant_subhalo->cold_halo_gas += subhalo->cold_halo_gas;
			descendant_subhalo->hot_halo_gas += subhalo->hot_halo_gas;
			descendant_subhalo->ejected_galaxy_gas += subhalo->ejected_galaxy_gas;
			if (subhalo->main_progenitor) {
				descendant_subhalo->cooling_subhalo_tracking = subhalo->cooling_subhalo_tracking;
			}

		}
	}

	// Now that descendants have been fully populated they should be correctly composed
	for(auto &halo: halos){
		for(auto &subhalo: halo->all_subhalos()) {
			if (!subhalo->descendant) {
				continue;
			}
			subhalo->descendant->check_subhalo_galaxy_composition();
		}
	}

	if (subhalos_without_descendant) {
		AllBaryons.baryon_total_lost[snapshot] = baryon_mass_loss;
		LOG(warning) << "Found " << subhalos_without_descendant << " subhalos without descendant while transferring galaxies.";
	}

}

void track_total_baryons(StarFormation &starformation, Cosmology &cosmology, ExecutionParameters execparams, SimulationParameters simulation_params, const std::vector<HaloPtr> &halos,
		TotalBaryon &AllBaryons, int snapshot, const molgas_per_galaxy &molgas, double deltat){


	BaryonBase mcold_total;
	BaryonBase mhothalo_total;
	BaryonBase mcoldhalo_total;
	BaryonBase mejectedhalo_total;
	BaryonBase mstars_total;
	BaryonBase mstars_bursts_galaxymergers;
	BaryonBase mstars_bursts_diskinstabilities;
	BaryonBase MBH_total;
	BaryonBase mHI_total;
	BaryonBase mH2_total;
	BaryonBase mDM_total;

	double SFR_total_disk = 0;
	double SFR_total_burst = 0;

	int number_major_mergers = 0;
	int number_minor_mergers = 0;
	int number_disk_instabil = 0;

	double z1 = simulation_params.redshifts[snapshot];
	double z2 = simulation_params.redshifts[snapshot+1];

	double mean_age = 0.5 * (cosmology.convert_redshift_to_age(z1) + cosmology.convert_redshift_to_age(z2));

	// Loop over all halos and subhalos to write galaxy properties
	for (auto &halo: halos){

		// accumulate dark matter mass
		mDM_total.mass += halo->Mvir;
        
		for (auto &subhalo: halo->all_subhalos()){
        
			// Accumulate subhalo baryons
			mhothalo_total.mass += subhalo->hot_halo_gas.mass;
			mhothalo_total.mass_metals += subhalo->hot_halo_gas.mass_metals;
        
			mcoldhalo_total.mass += subhalo->cold_halo_gas.mass;
			mcoldhalo_total.mass_metals += subhalo->cold_halo_gas.mass_metals;
        
			mejectedhalo_total.mass += subhalo->ejected_galaxy_gas.mass;
			mejectedhalo_total.mass_metals += subhalo->ejected_galaxy_gas.mass_metals;
        
			for (auto &galaxy: subhalo->galaxies){
       
				number_major_mergers += galaxy->interaction.major_mergers;
 				number_minor_mergers += galaxy->interaction.minor_mergers;
				number_disk_instabil += galaxy->interaction.disk_instabilities;

				if(execparams.output_sf_histories){
        
					galaxy->mean_stellar_age += (galaxy->sfr_disk + galaxy->sfr_bulge_mergers + galaxy->sfr_bulge_diskins) * deltat * mean_age;
					galaxy->total_stellar_mass_ever_formed += (galaxy->sfr_disk + galaxy->sfr_bulge_mergers + galaxy->sfr_bulge_diskins) * deltat;

					HistoryItem hist_galaxy;
					hist_galaxy.sfr_disk            = galaxy->sfr_disk;
					hist_galaxy.sfr_bulge_mergers   = galaxy->sfr_bulge_mergers;
					hist_galaxy.sfr_bulge_diskins   = galaxy->sfr_bulge_diskins;
					hist_galaxy.sfr_z_disk          = galaxy->sfr_z_disk;
					hist_galaxy.sfr_z_bulge_mergers = galaxy->sfr_z_bulge_mergers;
					hist_galaxy.sfr_z_bulge_diskins = galaxy->sfr_z_bulge_diskins;
					hist_galaxy.snapshot            = snapshot;
					galaxy->history.emplace_back(std::move(hist_galaxy));
				}
        
				//Accumulate galaxy baryons
				auto &molecular_gas = molgas.at(galaxy);
        
				mHI_total.mass += molecular_gas.m_atom + molecular_gas.m_atom_b;
				mH2_total.mass += molecular_gas.m_mol + molecular_gas.m_mol_b;
        
				mcold_total.mass += galaxy->disk_gas.mass + galaxy->bulge_gas.mass;
				mcold_total.mass_metals += galaxy->disk_gas.mass_metals + galaxy->bulge_gas.mass_metals;
        
				mstars_total.mass += galaxy->disk_stars.mass + galaxy->bulge_stars.mass;
				mstars_total.mass_metals += galaxy->disk_stars.mass_metals + galaxy->bulge_stars.mass_metals;
        
				mstars_bursts_galaxymergers.mass += galaxy->galaxymergers_burst_stars.mass;
				mstars_bursts_galaxymergers.mass_metals += galaxy->galaxymergers_burst_stars.mass_metals;
				mstars_bursts_diskinstabilities.mass += galaxy->diskinstabilities_burst_stars.mass;
				mstars_bursts_diskinstabilities.mass_metals += galaxy->diskinstabilities_burst_stars.mass_metals;

				SFR_total_disk  += galaxy->sfr_disk;
				SFR_total_burst += galaxy->sfr_bulge_mergers + galaxy->sfr_bulge_diskins;
        
				MBH_total.mass += galaxy->smbh.mass;
        
			}
		}
	}

	AllBaryons.mstars.push_back(mstars_total);
	AllBaryons.mstars_burst_galaxymergers.push_back(mstars_bursts_galaxymergers);
	AllBaryons.mstars_burst_diskinstabilities.push_back(mstars_bursts_diskinstabilities);
	AllBaryons.mcold.push_back(mcold_total);
	AllBaryons.mHI.push_back(mHI_total);
	AllBaryons.mH2.push_back(mH2_total);
	AllBaryons.mBH.push_back(MBH_total);
	AllBaryons.SFR_disk.push_back(SFR_total_disk);
	AllBaryons.SFR_bulge.push_back(SFR_total_burst);

	AllBaryons.major_mergers.push_back(number_major_mergers);
	AllBaryons.minor_mergers.push_back(number_minor_mergers);
	AllBaryons.disk_instabil.push_back(number_disk_instabil);

	AllBaryons.mhot_halo.push_back(mhothalo_total);
	AllBaryons.mcold_halo.push_back(mcoldhalo_total);
	AllBaryons.mejected_halo.push_back(mejectedhalo_total);

	AllBaryons.mDM.push_back(mDM_total);
}

}
