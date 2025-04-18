#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/redis_proxy/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

TEST(RedisProxyFilterConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RedisProxyFilterConfigFactory()
                   .createFilterFactoryFromProto(
                       envoy::extensions::filters::network::redis_proxy::v3::RedisProxy(), context)
                   .IgnoreError(),
               ProtoValidationException);
}

TEST(RedisProxyFilterConfigFactoryTest, NoUpstreamDefined) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings settings;
  settings.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(20));

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy config;
  config.set_stat_prefix("foo");
  config.mutable_settings()->CopyFrom(settings);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      RedisProxyFilterConfigFactory().createFilterFactoryFromProto(config, context).IgnoreError(),
      EnvoyException, "cannot configure a redis-proxy without any upstream");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoSettings) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
                          ProtoValidationException, "value is required");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyNoOpTimeout) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings: {}
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
                          ProtoValidationException, "embedded message failed validation");
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyCorrectProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
custom_commands: [example.parse]
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  EXPECT_TRUE(factory.isTerminalFilterByProto(proto_config, context.serverFactoryContext()));
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  EXPECT_CALL(context.server_factory_context_.cluster_manager_, grpcAsyncClientManager()).Times(0);
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyEmptyProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      *dynamic_cast<envoy::extensions::filters::network::redis_proxy::v3::RedisProxy*>(
          factory.createEmptyConfigProto().get());

  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyFaultProto) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
faults:
- fault_type: ERROR
  fault_enabled:
    default_value:
      numerator: 30
      denominator: HUNDRED
    runtime_key: "bogus_key"
  commands:
  - GET
- fault_type: DELAY
  fault_enabled:
    default_value:
      numerator: 20
      denominator: HUNDRED
    runtime_key: "bogus_key"
  delay: 2s
settings:
  op_timeout: 0.02s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  EXPECT_TRUE(factory.isTerminalFilterByProto(proto_config, context.serverFactoryContext()));
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Validates that a value of connection_rate_limit above 0 isn't rejected.
TEST(RedisProxyFilterConfigFactoryTest, ValidConnectionRateLimit) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  connection_rate_limit:
   connection_rate_limit_per_sec: 1
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  EXPECT_TRUE(factory.isTerminalFilterByProto(proto_config, context.serverFactoryContext()));
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

// Validates that a value of connection_rate_limit 0 is rejected.
TEST(RedisProxyFilterConfigFactoryTest, InvalidConnectionRateLimit) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  connection_rate_limit:
   connection_rate_limit_per_sec: 0
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config),
                          ProtoValidationException,
                          "ConnectionRateLimitPerSec: value must be greater than 0");
}

// Verify async gRPC client is created if external auth is enabled.
TEST(RedisProxyFilterConfigFactoryTest, ExternalAuthProvider) {
  const std::string yaml = R"EOF(
prefix_routes:
  catch_all_route:
    cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.02s
  connection_rate_limit:
   connection_rate_limit_per_sec: 1
external_auth_provider:
  grpc_service:
    envoy_grpc:
      cluster_name: "external_auth_cluster"
      )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  EXPECT_TRUE(factory.isTerminalFilterByProto(proto_config, context.serverFactoryContext()));
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  EXPECT_CALL(context.server_factory_context_.cluster_manager_, grpcAsyncClientManager());
  cb(connection);
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
