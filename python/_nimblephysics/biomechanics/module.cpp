/*
 * Copyright (c) 2011-2019, The DART development contributors
 * All rights reserved.
 *
 * The list of contributors can be found at:
 *   https://github.com/dartsim/dart/blob/master/LICENSE
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#include <Eigen/Dense>
#include <pybind11/eigen.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace dart {
namespace python {

void ForcePlate(py::module& sm);
void LilypadSolver(py::module& sm);
void BatchGaitInverseDynamics(py::module& sm);
void OpenSimParser(py::module& sm);
void SkeletonConverter(py::module& sm);
void MarkerFitter(py::module& sm);
void DynamicsFitter(py::module& sm);
void MarkerFixer(py::module& sm);
void MarkerLabeller(py::module& sm);
void IKErrorReport(py::module& sm);
void Anthropometrics(py::module& sm);
void C3DLoader(py::module& sm);
void SubjectOnDisk(py::module& sm);
void CortexStreaming(py::module& sm);
void StreamingMarkerTraces(py::module& sm);
void StreamingIK(py::module& sm);
void StreamingMocapLab(py::module& sm);
void MarkerBeamSearch(py::module& sm);
void MarkerMultiBeamSearch(py::module& sm);

void dart_biomechanics(py::module& m)
{
  auto sm = m.def_submodule("biomechanics");

  sm.doc()
      = "This provides biomechanics utilities in Nimble, including inverse "
        "dynamics and (eventually) mocap support and muscle estimation.";

  ForcePlate(sm);
  C3DLoader(sm);
  Anthropometrics(sm);
  LilypadSolver(sm);
  BatchGaitInverseDynamics(sm);
  OpenSimParser(sm);
  SkeletonConverter(sm);
  MarkerFixer(sm);
  MarkerFitter(sm);
  DynamicsFitter(sm);
  MarkerLabeller(sm);
  IKErrorReport(sm);
  SubjectOnDisk(sm);
  CortexStreaming(sm);
  StreamingMarkerTraces(sm);
  StreamingIK(sm);
  StreamingMocapLab(sm);
  MarkerBeamSearch(sm);
  MarkerMultiBeamSearch(sm);
}

} // namespace python
} // namespace dart
