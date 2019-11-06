#include "./fuerteclient.h"

#include <velocypack/Slice.h>

#include <cassert>
#include <iostream>

#include "velocypack/VelocyPackHelper.h"
#include "velocypack/velocypack-aliases.h"
FuerteClient::FuerteClient()
    : _lastHttpReturnCode(0),
      _lastErrorMessage(""),
      _version("arango"),
      _mode("unknown mode"),
      _role("UNKNOWN"),
      _loop(1),
      _vpackOptions(VPackOptions::Defaults),
      _forceJson(false) {
  _vpackOptions.buildUnindexedObjects = true;
  _vpackOptions.buildUnindexedArrays = true;
  _builder.onFailure(
      [this](arangodb::fuerte::Error error, std::string const &msg) {
        std::unique_lock<std::recursive_mutex> guard(_lock, std::try_to_lock);
        if (guard) {
          _lastHttpReturnCode = 503;
          _lastErrorMessage = msg;
        }
      });
}

FuerteClient::~FuerteClient() {
  _builder.onFailure(nullptr);  // reset callback
  shutdownConnection();
}

std::shared_ptr<arangodb::fuerte::Connection> FuerteClient::createConnection() {
  auto newConnection = _builder.connect(_loop);
  arangodb::fuerte::StringMap params{{"details", "true"}};
  auto req = arangodb::fuerte::createRequest(arangodb::fuerte::RestVerb::Get,
                                             "/_api/version", params);
  req->header.database = _databaseName;
  req->timeout(std::chrono::seconds(30));
  try {
    auto res = newConnection->sendRequest(std::move(req));

    _lastHttpReturnCode = res->statusCode();
    if (_lastHttpReturnCode >= 400) {
      auto const &headers = res->messageHeader().meta();
      auto it = headers.find("http/1.1");
      if (it != headers.end()) {
        _lastErrorMessage = (*it).second;
      }
    }

    if (_lastHttpReturnCode != 200) {
      return nullptr;
    }

    std::lock_guard<std::recursive_mutex> guard(_lock);
    _connection = newConnection;

    std::shared_ptr<VPackBuilder> parsedBody;
    VPackSlice body;
    if (res->contentType() == arangodb::fuerte::ContentType::VPack) {
      body = res->slice();
    } else if (res->contentType() == arangodb::fuerte::ContentType::Json) {
      parsedBody = arangodb::velocypack::Parser::fromJson(
          reinterpret_cast<char const *>(res->payload().data()),
          res->payload().size());
      body = parsedBody->slice();
    }
    if (!body.isObject()) {
      _lastErrorMessage = "invalid response";
      _lastHttpReturnCode = 503;
    }

    std::string const server =
        arangodb::basics::VelocyPackHelper::getStringValue(body, "server", "");

    // "server" value is a string and content is "arango"
    if (server == "arango") {
      // look up "version" value
      _version = arangodb::basics::VelocyPackHelper::getStringValue(
          body, "version", "");
      VPackSlice const details = body.get("details");
      if (details.isObject()) {
        VPackSlice const mode = details.get("mode");
        if (mode.isString()) {
          _mode = mode.copyString();
        }
        VPackSlice role = details.get("role");
        if (role.isString()) {
          _role = role.copyString();
        }
      }
      if (!body.hasKey("version")) {
        // if we don't get a version number in return, the server is
        // probably running in hardened mode
        return newConnection;
      }
      std::string const versionString =
          arangodb::basics::VelocyPackHelper::getStringValue(body, "version",
                                                             "");
      // not neccessary:
      /*std::pair<int, int> version =
          arangodb::rest::Version::parseVersionString(versionString);
      if (version.first < 3) {
        // major version of server is too low
        //_client->disconnect();
        shutdownConnection();
        _lastErrorMessage = "Server version number ('" + versionString +
                            "') is too low. Expecting 3.0 or higher";
        return newConnection;
      }*/
    }
    return _connection;
  } catch (arangodb::fuerte::Error const &e) {  // connection error
    _lastErrorMessage = arangodb::fuerte::to_string(e);
    _lastHttpReturnCode = 503;
    return nullptr;
  }
}

std::shared_ptr<arangodb::fuerte::Connection>
FuerteClient::acquireConnection() {
  std::lock_guard<std::recursive_mutex> guard(_lock);

  _lastErrorMessage = "";
  _lastHttpReturnCode = 0;

  if (!_connection ||
      (_connection->state() ==
           arangodb::fuerte::Connection::State::Disconnected ||
       _connection->state() == arangodb::fuerte::Connection::State::Failed)) {
    return createConnection();
  }
  return _connection;
}

void FuerteClient::setInterrupted(bool interrupted) {
  std::lock_guard<std::recursive_mutex> guard(_lock);
  if (interrupted && _connection != nullptr) {
    shutdownConnection();
  } else if (!interrupted &&
             (_connection == nullptr ||
              (_connection->state() ==
                   arangodb::fuerte::Connection::State::Disconnected ||
               _connection->state() ==
                   arangodb::fuerte::Connection::State::Failed))) {
    createConnection();
  }
}

bool FuerteClient::isConnected() const {
  std::lock_guard<std::recursive_mutex> guard(_lock);
  if (_connection) {
    return _connection->state() ==
           arangodb::fuerte::Connection::State::Connected;
  }
  return false;
}

void FuerteClient::connect(const std::string &endpoint,
                           const std::string &username,
                           const std::string &password) {
  /*
  TRI_ASSERT(client);
  std::lock_guard<std::recursive_mutex> guard(_lock);
  _forceJson = client->forceJson();

  _requestTimeout = std::chrono::duration<double>(client->requestTimeout());
  _databaseName = client->databaseName();
  _builder.endpoint(client->endpoint());
  // check jwtSecret first, as it is empty by default,
  // but username defaults to "root" in most configurations
  if (!client->jwtSecret().empty()) {
      _builder.jwtToken(
          fuerte::jwt::generateInternalToken(client->jwtSecret(), "arangosh"));
      _builder.authenticationType(fuerte::AuthenticationType::Jwt);
  } else if (!client->username().empty()) {
      _builder.user(client->username()).password(client->password());
      _builder.authenticationType(fuerte::AuthenticationType::Basic);
  }
  */
  _builder.endpoint(endpoint);
  _builder.user(username).password(password);
  _builder.authenticationType(arangodb::fuerte::AuthenticationType::Basic);
  createConnection();
}

void FuerteClient::reconnect() {
  std::lock_guard<std::recursive_mutex> guard(_lock);
  /*
  _requestTimeout = std::chrono::duration<double>(client->requestTimeout());
  _databaseName = client->databaseName();
  _builder.endpoint(client->endpoint());
  _forceJson = client->forceJson();
  // check jwtSecret first, as it is empty by default,
  // but username defaults to "root" in most configurations
  if (!client->jwtSecret().empty()) {
      _builder.jwtToken(
          fuerte::jwt::generateInternalToken(client->jwtSecret(), "arangosh"));
      _builder.authenticationType(fuerte::AuthenticationType::Jwt);
  } else if (!client->username().empty()) {
      _builder.user(client->username()).password(client->password());
      _builder.authenticationType(fuerte::AuthenticationType::Basic);
  }

  std::shared_ptr<fuerte::Connection> oldConnection;
  _connection.swap(oldConnection);
  if (oldConnection) {
      oldConnection->cancel();
  }
  oldConnection.reset();
  try {
      createConnection();
  } catch (...) {
      std::string errorMessage = "error in '" + client->endpoint() + "'";
      throw errorMessage;
  }

  if (isConnected() && _lastHttpReturnCode ==
  static_cast<int>(rest::ResponseCode::OK)) { LOG_TOPIC("2d416", INFO,
  arangodb::Logger::FIXME)
          << ClientFeature::buildConnectedMessage(endpointSpecification(),
  _version, _role, _mode, _databaseName, client->username()); } else { if
  (client->getWarnConnect()) { LOG_TOPIC("9d7ea", ERR, arangodb::Logger::FIXME)
              << "Could not connect to endpoint '" << client->endpoint()
              << "', username: '" << client->username() << "'";
      }

      std::string errorMsg = "could not connect";

      if (!_lastErrorMessage.empty()) {
          errorMsg = _lastErrorMessage;
      }

      throw errorMsg;
  }
  */
}

std::string FuerteClient::endpointSpecification() const {
  std::lock_guard<std::recursive_mutex> guard(_lock);
  if (_connection) {
    return _connection->endpoint();
  }
  return "";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief map of connections
////////////////////////////////////////////////////////////////////////////////

static std::unordered_map<void *, FuerteClient> Connections;

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for connections (call the destructor here)
////////////////////////////////////////////////////////////////////////////////

static void DestroyV8ClientConnection(FuerteClient *v8connection) {
  assert(v8connection != nullptr);

  auto it = Connections.find(v8connection);

  if (it != Connections.end()) {
    //(*it).second.Reset(); // ?
    Connections.erase(it);
  }

  delete v8connection;
}
std::unique_ptr<arangodb::fuerte::Response> FuerteClient::getData(
    const std::string &location,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Get, location, {},
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::headData(
    const std::string &location,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Head, location, {},
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::deleteData(
    const std::string &location, const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Delete, location, body,
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::optionsData(
    const std::string &location, const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Options, location, body,
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::postData(
    const std::string &location, const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Post, location, body,
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::putData(
    const std::string &location, const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Put, location, body,
                        headerFields);
}

std::unique_ptr<arangodb::fuerte::Response> FuerteClient::patchData(
    const std::string &location, const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  return requestDataRaw(arangodb::fuerte::RestVerb::Patch, location, body,
                        headerFields);
}
std::unique_ptr<arangodb::fuerte::Response> FuerteClient::requestDataRaw(
    arangodb::fuerte::RestVerb verb, const std::string &location,
    const arangodb::velocypack::Slice &body,
    const std::unordered_map<std::string, std::string> &headerFields) {
  bool retry = true;

again:
  // short method:
  //  auto request = arangodb::fuerte::createRequest(
  //      arangodb::fuerte::RestVerb::Post, "/_api/cursor");
  auto req = std::make_unique<arangodb::fuerte::Request>();
  req->header.restVerb = verb;
  req->header.database = _databaseName;
  req->header.parseArangoPath(location);
  // can also do:
  // req->header.parameters = headerFields;
  for (auto &pair : headerFields) {
    req->header.addMeta(pair.first, pair.second);
  }

  req->header.contentType(arangodb::fuerte::ContentType::VPack);
  req->addVPack(std::move(body));

  if (req->header.acceptType() == arangodb::fuerte::ContentType::Unset) {
    req->header.acceptType(arangodb::fuerte::ContentType::VPack);
  }
  req->timeout(
      std::chrono::duration_cast<std::chrono::milliseconds>(_requestTimeout));

  std::shared_ptr<arangodb::fuerte::Connection> connection =
      acquireConnection();
  if (!connection ||
      connection->state() == arangodb::fuerte::Connection::State::Failed) {
    // TRI_ERROR_SIMPLE_CLIENT_COULD_NOT_CONNECT
    throw std::runtime_error("not connected");
    // return false;
  }

  arangodb::fuerte::Error rc = arangodb::fuerte::Error::NoError;
  std::unique_ptr<arangodb::fuerte::Response> response;
  try {
    response = connection->sendRequest(std::move(req));
  } catch (arangodb::fuerte::Error const &e) {
    rc = e;
    _lastErrorMessage.assign(arangodb::fuerte::to_string(e));
    _lastHttpReturnCode = 503;
  }

  if (rc == arangodb::fuerte::Error::ConnectionClosed && retry) {
    retry = false;
    goto again;
  }

  // complete

  _lastHttpReturnCode = response->statusCode();

  if (_lastHttpReturnCode >= 400) {
  } else {
  }

  // and returns
  return response;
}
void FuerteClient::shutdownConnection() {
  std::lock_guard<std::recursive_mutex> guard(_lock);
  if (_connection) {
    _connection->cancel();
  }
}
