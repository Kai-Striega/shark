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
 * ODE solver class definition
 */

#ifndef SHARK_ODE_SOLVER_H_
#define SHARK_ODE_SOLVER_H_

#include <memory>
#include <vector>

#include <gsl/gsl_odeiv2.h>

namespace shark {

/**
 * A solver of ODE systems
 *
 * The ODE system solved by this class is defined in terms of an evaluation
 * function, a list of initial values `y0`, a time zero `t0` and a `delta_t`
 * parameter. After defining the solver, each call to the `next` method will
 * evolve the system, evaluating it at `t = t + delta_t`, with `t` starting at
 * `t0`, and returning the new values.
 */
class ODESolver {

public:

	/**
	 * The definition that ODE evaluators must follow
	 * @param
	 * @param y
	 * @param f
	 * @param A pointer to any user-provided data
	 * @return
	 */
	typedef int (*ode_evaluator)(double, const double y[], double f[], void *);

	/**
	 * Creates a new ODESolver
	 *
	 * @param y0 The initial values for the ODE system. It should contain as
	 * many values as those produced by `evaluator`
	 * @param t0 The time associated with the initial values `y0`.
	 * @param delta_t The time difference used to evolve the system on each
	 * evaluation
	 * @param precision The precision to use for the adaptive step sizes.
	 * @param evaluator The function evaluating the system at time `t`
	 */
	ODESolver(const std::vector<double> &y0, double t0, double delta_t,
	          double precision, ode_evaluator evaluator);

	/**
	 * Creates a new ODESolver
	 *
	 * @param y0 The initial values for the ODE system. It should contain as
	 * many values as those produced by `evaluator`
	 * @param t0 The time associated with the initial values `y0`.
	 * @param delta_t The time difference used to evolve the system on each
	 * evaluation
	 * @param ode_system The (GSL) ODE system to solve
	 * @param precision The precision to use for the adaptive step sizes.
	 */
	ODESolver(const std::vector<double> &y0, double t0, double delta_t,
	          double precision, const std::shared_ptr<gsl_odeiv2_system> &ode_system);

	/**
	 * Move constructor
	 */
	ODESolver(ODESolver &&odeSolver);

	/**
	 * Destructs this solver and frees up all resources associated with it
	 */
	~ODESolver();

	/**
	 * Evolves the ODE system by evaluating it in the new `t = t + delta_t`
	 *
	 * @return The `y` values for the evaluation of the system at `t`.
	 */
	std::vector<double> evolve();

	/**
	 * Returns the number of times that the internal ODE system has been
	 * evaluated so far.
	 *
	 * @return The number of times the ODE system has been evaluated.
	 */
	unsigned long int num_evaluations();

	/**
	 * Returns the current time `t` at which the system is sitting
	 * @return
	 */
	double current_t() {
		return t;
	}

	/**
	 * Move assignment operator
	 */
	ODESolver& operator=(ODESolver &&other);

private:
	std::vector<double> y;
	double t;
	double t0;
	double delta_t;
	unsigned int step;
	std::shared_ptr<gsl_odeiv2_system> ode_system;
	std::unique_ptr<gsl_odeiv2_driver> driver;
};

}  // namespace shark

#endif // SHARK_ODE_SOLVER_H_