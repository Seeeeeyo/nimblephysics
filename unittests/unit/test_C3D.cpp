#include <algorithm> // std::sort
#include <vector>

#include <Eigen/Dense>
#include <ccd/ccd.h>
#include <gtest/gtest.h>
#include <math.h>

#include "dart/biomechanics/C3DLoader.hpp"
#include "dart/biomechanics/OpenSimParser.hpp"
#include "dart/common/LocalResourceRetriever.hpp"
#include "dart/common/ResourceRetriever.hpp"
#include "dart/common/Uri.hpp"
#include "dart/math/MathTypes.hpp"
#include "dart/server/GUIWebsocketServer.hpp"
#include "dart/utils/C3D.hpp"
#include "dart/utils/CompositeResourceRetriever.hpp"
#include "dart/utils/DartResourceRetriever.hpp"
#include "dart/utils/PackageResourceRetriever.hpp"

#include "GradientTestUtils.hpp"
#include "TestHelpers.hpp"

using namespace dart;

#define ALL_TESTS

#ifdef ALL_TESTS
TEST(C3D, COMPARE_TO_TRC)
{
  biomechanics::C3D c3d
      = biomechanics::C3DLoader::loadC3D("dart://sample/c3d/JA1Gait35.c3d");
  biomechanics::OpenSimTRC trc = biomechanics::OpenSimParser::loadTRC(
      "dart://sample/osim/Sprinter/run0900cms.trc");

  EXPECT_EQ(c3d.markerTimesteps.size(), trc.markerTimesteps.size());
  for (int i = 0; i < trc.markerTimesteps.size(); i++)
  {
    EXPECT_TRUE(c3d.markerTimesteps[i].size() <= trc.markerTimesteps[i].size());
    for (auto& pair : c3d.markerTimesteps[i])
    {
      if (trc.markerTimesteps[i].count(pair.first) == 0)
      {
        EXPECT_TRUE(trc.markerTimesteps[i].count(pair.first) > 0);
        return;
      }

      Eigen::Vector3s c3dVec = c3d.markerTimesteps[i][pair.first];
      Eigen::Vector3s trcVec = trc.markerTimesteps[i][pair.first];

      if (!equals(c3dVec, trcVec, 1e-9))
      {
        std::cout << "Mismatch on frame " << i << ":" << pair.first
                  << std::endl;
        std::cout << "TRC: " << std::endl << trcVec << std::endl;
        std::cout << "C3D: " << std::endl << c3dVec << std::endl;
        std::cout << "Diff: " << std::endl << trcVec - c3dVec << std::endl;
        EXPECT_TRUE(equals(c3dVec, trcVec, 1e-9));
        return;
      }
    }
  }
}
#endif

#ifdef ALL_TESTS
TEST(C3D, JA1GAIT35_GRF_CHECK)
{
  biomechanics::C3D c3d
      = biomechanics::C3DLoader::loadC3D("dart://sample/c3d/JA1Gait35.c3d");
  for (int i = 0; i < c3d.forcePlates.size(); i++)
  {
    for (int t = 0; t < c3d.forcePlates[i].timestamps.size(); t++)
    {
      if (c3d.forcePlates[i].forces[t].hasNaN())
      {
        std::cout << "Force plate " << i << " has NaN force on time " << t
                  << std::endl;
        return;
      }
      if (c3d.forcePlates[i].moments[t].hasNaN())
      {
        std::cout << "Force plate " << i << " has NaN force on time " << t
                  << std::endl;
        return;
      }
      if (c3d.forcePlates[i].centersOfPressure[t].hasNaN())
      {
        std::cout << "Force plate " << i << " has NaN force on time " << t
                  << std::endl;
        return;
      }
    }
  }
}
#endif

#ifdef ALL_TESTS
TEST(C3D, TEST_VERTICAL_CONVENTION)
{
  biomechanics::C3D c3d = biomechanics::C3DLoader::loadC3D(
      "dart://sample/grf/UpsideDownData/trial1.c3d");
  Eigen::Vector3s sum = Eigen::Vector3s::Zero();
  for (int i = 0; i < c3d.forcePlates.size(); i++)
  {
    for (int t = 0; t < c3d.forcePlates[i].timestamps.size(); t++)
    {
      sum += c3d.forcePlates[i].forces[t];
    }
  }
  // We expect the total force to be pointing upwards, overall
  EXPECT_GE(sum(1), 0);
}
#endif

/*
#ifdef ALL_TESTS
TEST(C3D, TEST_NO_NAMED_MARKERS)
{
  biomechanics::C3D c3d = biomechanics::C3DLoader::loadC3D(
      "dart://sample/c3d/CheeseburgerTrial000001.c3d");
  for (auto& pair : c3d.markerTimesteps[0])
  {
    std::cout << pair.first << std::endl;
  }
}
#endif
*/