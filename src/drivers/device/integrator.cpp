/****************************************************************************
 *
 *   Copyright (c) 2015-2016 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file integrator.cpp
 *
 * A resettable integrator
 *
 * @author Lorenz Meier <lorenz@px4.io>
 * @author Julian Oes <julian@oes.ch>
 */

#include "integrator.h"

Integrator::Integrator(uint64_t auto_reset_interval, bool coning_compensation) :
	_auto_reset_interval(auto_reset_interval),
	_last_integration_time(0),
	_last_reset_time(0),
	_alpha(0.0f, 0.0f, 0.0f),
	_last_alpha(0.0f, 0.0f, 0.0f),
	_beta(0.0f, 0.0f, 0.0f),
	_last_val(0.0f, 0.0f, 0.0f),
	_last_delta_alpha(0.0f, 0.0f, 0.0f),
	_coning_comp_on(coning_compensation)
{

}

Integrator::~Integrator()
{

}

bool
Integrator::put(uint64_t timestamp, math::Vector<3> &val, math::Vector<3> &integral, uint64_t &integral_dt)
{
	if (_last_integration_time == 0) {
		/* this is the first item in the integrator */
		_last_integration_time = timestamp;
		_last_reset_time = timestamp;
		_last_val = val;

		return false;
	}

	double dt = 0.0;

	// Integrate:
	// Leave dt at 0 if the integration time does not make sense.
	// Without this check the integral is likely to explode.
	if (timestamp >= _last_integration_time) {
		dt = (double)(timestamp - _last_integration_time) / 1000000.0;
	}

	// Use trapezoidal integration to calculate the delta integral
	math::Vector<3> delta_alpha = (val + _last_val) * dt * 0.5f;
	_last_val = val;

	// Calculate coning corrections if required
	if (_coning_comp_on) {
		// Coning compensation derived by Paul Riseborough and Jonathan Challinger,
		// following:
		// Tian et al (2010) Three-loop Integration of GPS and Strapdown INS with Coning and Sculling Compensation
		// Sourced: http://www.sage.unsw.edu.au/snap/publications/tian_etal2010b.pdf
		// Simulated: https://github.com/priseborough/InertialNav/blob/master/models/imu_error_modelling.m
		_beta += ((_last_alpha + _last_delta_alpha * (1.0f / 6.0f)) % delta_alpha) * 0.5f;
		_last_delta_alpha = delta_alpha;
		_last_alpha = _alpha;
	}

	// accumulate delta integrals
	_alpha += delta_alpha;

	_last_integration_time = timestamp;

	// Only do auto reset if auto reset interval is not 0.
	if (_auto_reset_interval > 0 && (timestamp - _last_reset_time) > _auto_reset_interval) {

		// apply coning corrections if required
		if (_coning_comp_on) {
			integral = _alpha + _beta;

		} else {
			integral = _alpha;
		}

		// reset the integrals and coning corrections
		_reset(integral_dt);

		return true;

	} else {
		return false;
	}
}

bool
Integrator::put_with_interval(unsigned interval_us, math::Vector<3> &val, math::Vector<3> &integral,
			      uint64_t &integral_dt)
{
	if (_last_integration_time == 0) {
		/* this is the first item in the integrator */
		uint64_t now = hrt_absolute_time();
		_last_integration_time = now;
		_last_reset_time = now;
		_last_val = val;

		return false;
	}

	// Create the timestamp artifically.
	uint64_t timestamp = _last_integration_time + interval_us;

	return put(timestamp, val, integral, integral_dt);
}

math::Vector<3>
Integrator::get(bool reset, uint64_t &integral_dt)
{
	math::Vector<3> val = _alpha;

	if (reset) {
		_reset(integral_dt);
	}

	return val;
}

math::Vector<3>
Integrator::get_and_filtered(bool reset, uint64_t &integral_dt, math::Vector<3> &filtered_val)
{
	// Do the usual get with reset first but don't return yet.
	math::Vector<3> ret_integral = get(reset, integral_dt);

	// Because we need both the integral and the integral_dt.
	filtered_val(0) = ret_integral(0) * 1000000 / integral_dt;
	filtered_val(1) = ret_integral(1) * 1000000 / integral_dt;
	filtered_val(2) = ret_integral(2) * 1000000 / integral_dt;

	return ret_integral;
}

void
Integrator::_reset(uint64_t &integral_dt)
{
	_alpha(0) = 0.0f;
	_alpha(1) = 0.0f;
	_alpha(2) = 0.0f;
	_last_alpha(0) = 0.0f;
	_last_alpha(1) = 0.0f;
	_last_alpha(2) = 0.0f;
	_beta(0) = 0.0f;
	_beta(1) = 0.0f;
	_beta(2) = 0.0f;

	integral_dt = (_last_integration_time - _last_reset_time);
	_last_reset_time = _last_integration_time;
}
