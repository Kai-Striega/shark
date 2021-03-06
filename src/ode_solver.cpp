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
 * ODE Solver class implementation
 */

#include <sstream>
#include <vector>

#include "exceptions.h"
#include "logging.h"
#include "ode_solver.h"

#include <gsl/gsl_errno.h>

using namespace std;

namespace shark {

ODESolver::ODESolver(const std::vector<double> &y0, double t0, double delta_t, double precision, ode_evaluator evaluator) :
	y(y0),
	t(t0),
	t0(t0),
	delta_t(delta_t),
	step(0),
	ode_system(),
	driver()
{
	ode_system = std::shared_ptr<gsl_odeiv2_system>(new gsl_odeiv2_system{evaluator, NULL, y0.size(), NULL});
	driver.reset(gsl_odeiv2_driver_alloc_y_new(ode_system.get(), gsl_odeiv2_step_rkck, delta_t, 0.0, precision));
}

ODESolver::ODESolver(const std::vector<double> &y0, double t0, double delta_t, double precision, const std::shared_ptr<gsl_odeiv2_system> &ode_system) :
	y(y0),
	t(t0),
	t0(t0),
	delta_t(delta_t),
	step(0),
	ode_system(ode_system),
	driver()
{
	driver.reset(gsl_odeiv2_driver_alloc_y_new(ode_system.get(), gsl_odeiv2_step_rkck, delta_t, 0.0, precision));
}

ODESolver::ODESolver(ODESolver &&odeSolver) :
	y(odeSolver.y),
	t(odeSolver.t),
	t0(odeSolver.t0),
	delta_t(odeSolver.delta_t),
	step(odeSolver.step),
	ode_system(odeSolver.ode_system),
	driver()
{
	std::swap(driver, odeSolver.driver);
}

ODESolver::~ODESolver() {
	if (driver) {
		gsl_odeiv2_driver_free(driver.release());
	}
}

std::vector<double> ODESolver::evolve() {

	step++;
	double t_i = t0 + step*delta_t;
	int status = gsl_odeiv2_driver_apply(driver.get(), &t, t_i, y.data());

	// TODO: add compiler-dependent likelihood macro
	if (status == GSL_SUCCESS) {
		return y;
	}

	//TEST: forcing integration to finish regardless of accuracy issue in three cases.

	ostringstream os;
	os << "Error while solving ODE system: ";
	if (status == GSL_FAILURE) {
		os << "step size decreases below machine precision ";
		LOG(warning) << "ODE: step size decreases below machine precision. Will force integration to finish regardless of desired accuracy not reached.";
		status = GSL_SUCCESS;
		return y;
	}
	if (status == GSL_ENOPROG) {
		os << "step size dropped below minimum value";
		LOG(warning) << "ODE:step size dropped below minimum value. Will force integration to finish regardless of desired accuracy not reached.";
		status = GSL_SUCCESS;
		return y;
	}
	else if (status == GSL_EBADFUNC) {
		os << "user function signaled an error";
		gsl_odeiv2_driver_reset(driver.get());
	}
	else if (status == GSL_EMAXITER) {
		os << "maximum number of steps reached";
		LOG(warning) << "ODE:maximum number of steps reached. Will force integration to finish regardless of desired accuracy not reached.";
		status = GSL_SUCCESS;
		return y;
	}
	else {
		os << "unexpected GSL error: " << gsl_strerror(status);
	}

	throw math_error(os.str());
}

unsigned long int ODESolver::num_evaluations()
{
	return driver->n;
}

ODESolver &ODESolver::operator=(ODESolver &&other) {

	// Normal moving of values
	y = other.y;
	t0 = other.t0;
	t = other.t;
	delta_t = other.delta_t;
	step = other.step;
	ode_system = other.ode_system;
	std::swap(driver, other.driver);
	return *this;
}

}  // namespace shark
