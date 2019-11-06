#ifndef FUERTECLIENT_H
#define FUERTECLIENT_H

#include <velocypack/StringRef.h>
//#include <fuerte/fuerte.h>
#include <fuerte/connection.h>
#include <fuerte/loop.h>
#include <fuerte/requests.h>
#include <fuerte/types.h>

class FuerteClient {
  FuerteClient(FuerteClient const&) = delete;
  FuerteClient& operator=(FuerteClient const&) = delete;

 public:
  explicit FuerteClient();
  ~FuerteClient();

 public:
  void setInterrupted(bool interrupted);
  bool isConnected() const;

  void connect(const std::string& endpoint, const std::string& username,
               const std::string& password);
  void reconnect();

  double timeout() const { return _requestTimeout.count(); }

  void timeout(double value) {
    _requestTimeout = std::chrono::duration<double>(value);
  }

  std::string const& databaseName() const { return _databaseName; }
  void setDatabaseName(std::string const& value) { _databaseName = value; }
  void setForceJson(bool value) { _forceJson = value; };
  std::string username() const { return _builder.user(); }
  std::string password() const { return _builder.password(); }
  int lastHttpReturnCode() const { return _lastHttpReturnCode; }
  std::string lastErrorMessage() const { return _lastErrorMessage; }
  std::string const& version() const { return _version; }
  std::string const& mode() const { return _mode; }
  std::string const& role() const { return _role; }
  std::string endpointSpecification() const;

  // application_features::ApplicationServer& server();

  std::unique_ptr<arangodb::fuerte::Response> getData(
      const std::string& location,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> headData(
      const std::string& location,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> deleteData(
      const std::string& location, const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> optionsData(
      const std::string& location, const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> postData(
      const std::string& location, const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> putData(
      const std::string& location, const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);

  std::unique_ptr<arangodb::fuerte::Response> patchData(
      const std::string& location, const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);

  void initServer();

 private:
  std::unique_ptr<arangodb::fuerte::Response> requestDataRaw(
      arangodb::fuerte::RestVerb verb, const std::string& location,
      const arangodb::velocypack::Slice& body,
      std::unordered_map<std::string, std::string> const& headerFields);
  std::shared_ptr<arangodb::fuerte::Connection> createConnection();
  std::shared_ptr<arangodb::fuerte::Connection> acquireConnection();
  //    v8::Local<v8::Value> handleResult(
  //                          std::unique_ptr<arangodb::fuerte::Response>
  //                          response, arangodb::fuerte::Error ec);
  /// @brief shuts down the connection _connection and resets the pointer
  /// to a nullptr
  void shutdownConnection();

 private:
  // application_features::ApplicationServer& _server;

  std::string _databaseName;
  std::chrono::duration<double> _requestTimeout;

  mutable std::recursive_mutex _lock;
  int _lastHttpReturnCode;
  std::string _lastErrorMessage;
  std::string _version;
  std::string _mode;
  std::string _role;

  arangodb::fuerte::EventLoopService _loop;
  arangodb::fuerte::ConnectionBuilder _builder;
  std::shared_ptr<arangodb::fuerte::Connection> _connection;
  arangodb::velocypack::Options _vpackOptions;
  bool _forceJson;
};

#endif  // FUERTECLIENT_H
