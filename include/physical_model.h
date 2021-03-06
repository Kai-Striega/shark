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
 *
 * Physical model definition
 */

#ifndef SHARK_SYSTEM_H_
#define SHARK_SYSTEM_H_

#include <memory>
#include <sstream>
#include <stdexcept>

#include <gsl/gsl_odeiv2.h>
#include <recycling.h>
#include "components.h"
#include "gas_cooling.h"
#include "numerical_constants.h"
#include "ode_solver.h"
#include "stellar_feedback.h"
#include "star_formation.h"

namespace shark {

template <int NC>
class PhysicalModel {

public:

	/**
	 * The set of parameters passed down to the ODESolver. It includes the
	 * physical model itself, the galaxy and subhalo being evolved on each call,
	 * and other various values.
	 */
	struct solver_params {
		PhysicalModel<NC> &model;
		double rgas;
		double rstar;
		double mcoolrate;
		double jcold_halo;
		double delta_t;
		double redshift;
		double vsubh;
		double vgal;
		bool   burst;
	};

	PhysicalModel(
			double ode_solver_precision,
			ODESolver::ode_evaluator evaluator,
			GasCooling gas_cooling) :
		evaluator(evaluator),
		ode_solver_precision(ode_solver_precision),
		gas_cooling(gas_cooling),
		galaxy_ode_evaluations(0),
		galaxy_starburst_ode_evaluations(0)
	{
		// no-op
	}

	virtual ~PhysicalModel()
	{
		// no-op
	}

	ODESolver get_solver(double delta_t, const std::vector<double> &y0, solver_params &params) {
		if (y0.size() != NC) {
			std::ostringstream os;
			os << "# initial values != ODE components: " << y0.size() << " != " << NC;
			throw std::invalid_argument(os.str());
		}

		auto system_ptr = std::shared_ptr<gsl_odeiv2_system>(new gsl_odeiv2_system{evaluator, nullptr, NC, &params});
		return ODESolver(y0, 0, delta_t, ode_solver_precision, system_ptr);
	}

	void evolve_galaxy(Subhalo &subhalo, Galaxy &galaxy, double z, double delta_t)
	{
		/**
		 * Parameters that are needed as input in the ode_solver:
		 * mcoolrate: gas cooling rate onto galaxy [Msun/Gyr/h]
		 * rgas: half-gas mass radius of the disk [Mpc/h]
		 * vgal: disk velocity at rgas [km/s]
		 * rstar: half-stellar mass radius of the disk [Mpc/h]
		 * vsubh: virial velocity of the host subhalo [km/s]
		 * jcold_halo: specific angular momentum of the cooling gas [Msun/h Mpc/h km/s]
		 * burst: boolean parameter indicating if this is a starburst or not.
		 */

		double mcoolrate = 0;
		// Define cooling rate only in the case galaxy is central.
		if(galaxy.galaxy_type == Galaxy::CENTRAL){
			mcoolrate = gas_cooling.cooling_rate(subhalo, galaxy, z, delta_t);
		}

		double rgas       = galaxy.disk_gas.rscale; //gas scale radius.
		double vgal       = galaxy.disk_gas.sAM / galaxy.disk_gas.rscale * constants::EAGLEJconv;

		// Catch cases where gas disk doesn't exist yet.
		if(rgas <= 0){
			//In this case assign a scalelength due to the cooling gas.
			rgas = subhalo.cold_halo_gas.sAM / galaxy.vmax * constants::EAGLEJconv;
			vgal = galaxy.vmax;
		}

		double rstar      = galaxy.disk_stars.rscale; //stellar scale radius.
		double vsubh      = subhalo.Vvir;
		double jcold_halo = subhalo.cold_halo_gas.sAM;
		bool   burst      = false;

		std::vector<double> y0 = from_galaxy(subhalo, galaxy);
		solver_params params{*this, rgas, rstar, mcoolrate, jcold_halo, delta_t, z, vsubh, vgal, burst};
		auto ode_solver = get_solver(delta_t, y0, params);
		std::vector<double> y1 = ode_solver.evolve();
		galaxy_ode_evaluations += ode_solver.num_evaluations();
		to_galaxy(y1, subhalo, galaxy, delta_t);
	}

	void evolve_galaxy_starburst(Subhalo &subhalo, Galaxy &galaxy, double z, double delta_t, bool from_galaxy_merger)
	{

		/**
		 * Parameters that are needed as input in the ode_solver:
		 * mcoolrate: gas cooling rate onto galaxy [Msun/Gyr/h]. In the case of starbursts, this is \equiv 0
		 * rgas: half-gas mass radius of the bulge [Mpc/h]
		 * vgal: bulge velocity at rgas [km/s]
		 * rstar: half-stellar mass radius of the bulge [Mpc/h]
		 * vsubh: virial velocity of the host subhalo [km/s]
		 * jcold_halo: specific angular momentum of the cooling gas [Msun/h Mpc/h km/s]
		 * burst: boolean parameter indicating if this is a starburst or not.
		 */

		double mcoolrate  = 0; //During central starbursts, cooling rate =0, as cooling gas always settles in the disk (not the bulge).
		double jcold_halo = 0; //Same as above.
		double rgas       = galaxy.bulge_gas.rscale; //gas scale radius.
		double rstar      = galaxy.bulge_stars.rscale; //stellar scale radius.
		double vsubh      = subhalo.Vvir;
		double vgal       = galaxy.bulge_gas.sAM / galaxy.bulge_gas.rscale;
		bool   burst      = true;

		std::vector<double> y0 = from_galaxy_starburst(subhalo, galaxy);
		solver_params params{*this, rgas, rstar, mcoolrate, jcold_halo, delta_t, z, vsubh, vgal, burst};
		auto solver = get_solver(delta_t, y0, params);
		std::vector<double> y1 = solver.evolve();
		galaxy_starburst_ode_evaluations += solver.num_evaluations();
		to_galaxy_starburst(y1, subhalo, galaxy, delta_t, from_galaxy_merger);
	}

	virtual std::vector<double> from_galaxy(const Subhalo &subhalo, const Galaxy &galaxy) = 0;
	virtual void to_galaxy(const std::vector<double> &y, Subhalo &subhalo, Galaxy &galaxy, double delta_t) = 0;

	virtual std::vector<double> from_galaxy_starburst(const Subhalo &subhalo, const Galaxy &galaxy) = 0;
	virtual void to_galaxy_starburst(const std::vector<double> &y, Subhalo &subhalo, Galaxy &galaxy, double delta_t, bool from_galaxy_merger) = 0;

	unsigned long int get_galaxy_ode_evaluations() {
		return galaxy_ode_evaluations;
	}

	unsigned long int get_galaxy_starburst_ode_evaluations() {
		return galaxy_starburst_ode_evaluations;
	}

	void reset_ode_evaluations() {
		galaxy_ode_evaluations = 0;
		galaxy_starburst_ode_evaluations = 0;
	}

private:
	ODESolver::ode_evaluator evaluator;
	double ode_solver_precision;
	GasCooling gas_cooling;
	unsigned long int galaxy_ode_evaluations;
	unsigned long int galaxy_starburst_ode_evaluations;
};

class BasicPhysicalModel : public PhysicalModel<17> {
public:
	BasicPhysicalModel(double ode_solver_precision,
			GasCooling gas_cooling,
			StellarFeedback stellar_feedback,
			StarFormation star_formation,
			RecyclingParameters recycling_parameters,
			GasCoolingParameters gas_cooling_parameters);

	std::vector<double> from_galaxy(const Subhalo &subhalo, const Galaxy &galaxy);
	void to_galaxy(const std::vector<double> &y, Subhalo &subhalo, Galaxy &galaxy, double delta_t);

	std::vector<double> from_galaxy_starburst(const Subhalo &subhalo, const Galaxy &galaxy);
	void to_galaxy_starburst(const std::vector<double> &y, Subhalo &subhalo, Galaxy &galaxy, double delta_t, bool from_galaxy_merger);

	StellarFeedback stellar_feedback;
	StarFormation star_formation;
	RecyclingParameters recycling_parameters;
	GasCoolingParameters gas_cooling_parameters;

	void reset_ode_evaluations() {
		PhysicalModel::reset_ode_evaluations();
		star_formation.reset_integration_intervals();
	}

	unsigned long int get_star_formation_integration_intervals() {
		return star_formation.get_integration_intervals();
	}

};

}  // namespace shark

#endif // SHARK_SYSTEM_H_
