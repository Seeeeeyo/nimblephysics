dart_add_test("regression" test_Issue000Template test_Issue000Template.cpp)

dart_add_test("regression" test_Issue1243 test_Issue1243.cpp)

if(TARGET dart-utils)
  dart_add_test("regression" test_Arnold2013Synthetic_IKInitializer test_Arnold2013Synthetic_IKInitializer)
  target_link_libraries(test_Arnold2013Synthetic_IKInitializer dart-utils)

  dart_add_test("regression" test_Arnold2013Synthetic_MarkerFitter test_Arnold2013Synthetic_MarkerFitter)
  target_link_libraries(test_Arnold2013Synthetic_MarkerFitter dart-utils)
endif()

if(TARGET dart-utils-urdf)
  dart_add_test("regression" test_Issue838)
  target_link_libraries(test_Issue838 dart-utils-urdf)

  dart_add_test("regression" test_IntermediateWeldJoint)
  target_link_libraries(test_IntermediateWeldJoint dart-utils)
  target_link_libraries(test_IntermediateWeldJoint dart-utils-urdf)

  dart_add_test("regression" test_Issue892)

  dart_add_test("regression" test_Issue895)

  dart_add_test("regression" test_Issue986)

  dart_add_test("regression" test_Issue1231)
endif()

if(TARGET dart-collision-bullet)
  dart_add_test("regression" test_Issue1184)
  target_link_libraries(test_Issue1184 dart-collision-bullet)
endif()

if(TARGET dart-collision-bullet AND TARGET dart-collision-ode)
  dart_add_test("regression" test_Issue1234)
  target_link_libraries(test_Issue1234
    dart-collision-bullet
    dart-collision-ode
    dart-utils)
endif()

dart_format_add(test_Issue892.cpp test_Issue1243.cpp)
