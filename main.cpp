#include <fuerte/fuerte.h>
#include <velocypack/Slice.h>

#include <iostream>

using namespace std;
#import "velocypack/velocypack-aliases.h"

int main() {
  arangodb::fuerte::EventLoopService eventLoopService;
  auto conn =
      arangodb::fuerte::ConnectionBuilder()
          .endpoint("vst://localhost:8529")
          .user("root")
          .password("root")
          .authenticationType(arangodb::fuerte::AuthenticationType::Basic)
          .connect(eventLoopService);
  // on main readme page:
  try {
    auto request = arangodb::fuerte::createRequest(
        arangodb::fuerte::RestVerb::Get, "/_api/version");
    auto result = conn->sendRequest(std::move(request));
    auto slice = result->slices().front();
    // std::cout << slice.toJson();
    std::cout << slice.get("version").copyString() << std::endl;
    std::cout << slice.get("server").copyString() << std::endl;
  } catch (arangodb::velocypack::Exception &e) {
    std::cout << e.what() << std::endl;
  }

  // https://github.com/arangodb/fuerte/blob/master/tests/test_connection_basic.cpp#L113
  namespace fu = ::arangodb::fuerte;
  {
    auto request = fu::createRequest(fu::RestVerb::Post, "/_api/cursor");
    VPackBuilder builder;
    builder.openObject();
    builder.add("query", VPackValue("FOR x IN 1..5 RETURN x"));
    builder.close();
    request->addVPack(builder.slice());
    auto response = conn->sendRequest(std::move(request));
    // ASSERT_EQ(response->statusCode(), fu::StatusCreated);
    auto slice = response->slices().front();
    std::cout << slice.toJson() << std::endl;

    // ASSERT_TRUE(slice.isObject());
    auto result = slice.get("result");
    // ASSERT_TRUE(result.isArray());
    // ASSERT_TRUE(result.length() == 5);
    std::cout << result.length() << std::endl;
  }  // namespace fu=::arangodb::fuerte;
  return 0;
}
