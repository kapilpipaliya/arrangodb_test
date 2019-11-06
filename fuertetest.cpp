#include "./fuertetest.h"

#include <iostream>

#include "./fuerteclient.h"
#import "velocypack/velocypack-aliases.h"

FuerteTest::FuerteTest() {
  test1();
  test2();
}

void FuerteTest::test1() {
  FuerteClient f;
  f.connect("vst://localhost:8529", "root", "root");
  auto response = f.getData("/_api/version", {});

  auto slice = response->slices().front();
  std::cout << slice.get("version").copyString() << std::endl;
  std::cout << slice.get("server").copyString() << std::endl;
}

void FuerteTest::test2() {
  FuerteClient f;
  f.connect("vst://localhost:8529", "root", "root");

  VPackBuilder builder;
  builder.openObject();
  builder.add("query", VPackValue("FOR x IN 1..5 RETURN x"));
  builder.close();

  auto response = f.postData("/_api/cursor", builder.slice(), {});

  // ASSERT_EQ(response->statusCode(), fu::StatusCreated);
  auto slice = response->slices().front();
  std::cout << slice.toJson() << std::endl;

  // ASSERT_TRUE(slice.isObject());
  auto result = slice.get("result");
  // ASSERT_TRUE(result.isArray());
  // ASSERT_TRUE(result.length() == 5);
  std::cout << result.length() << std::endl;
}
