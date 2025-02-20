#ifndef BIOMECHANICS_ENUMS_HPP
#define BIOMECHANICS_ENUMS_HPP

namespace dart {
namespace biomechanics {

enum MissingGRFReason
{
  notMissingGRF,
  // These are the legacy filter's reasons
  measuredGrfZeroWhenAccelerationNonZero,
  unmeasuredExternalForceDetected,
  footContactDetectedButNoForce,
  torqueDiscrepancy,
  forceDiscrepancy,
  notOverForcePlate,
  missingImpact,
  missingBlip,
  shiftGRF,
  manualReview,
  interpolatedClippedGRF,
  // These are the new filter's reasons
  tooHighMarkerRMS,
  hasInputOutliers,
  hasNoForcePlateData,
  velocitiesStillTooHighAfterFiltering,
  copOutsideConvexFootError,
  zeroForceFrame,
  extendedToNearestPeakForce
};

enum BasicTrialType
{
  treadmill,
  overground,
  staticTrial,
  other
};

enum DetectedTrialFeature
{
  walking,
  running,
  unevenTerrain,
  flatTerrain
};

enum DataQuality
{
  pilotData,
  experimentalData,
  internetData
};

enum MissingGRFStatus
{
  no = 0,      // no will cast to `false`
  unknown = 1, // unknown will cast to `true`
  yes = 2,     // yes will cast to `true`
};

enum ProcessingPassType
{
  kinematics,
  dynamics,
  lowPassFilter,
  accMinimizingFilter
};

} // namespace biomechanics
} // namespace dart

#endif // BIOMECHANICS_ENUMS_HPP
