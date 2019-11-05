cmake_minimum_required(VERSION 3.15)

project(untitled LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

find_package(Boost REQUIRED COMPONENTS "system" "thread")
find_package(OpenSSL REQUIRED)

add_definitions("-DVELOCYPACK_XXHASH=1")
include_directories(${OPENSSL_INCLUDE_DIR})

#include_directories(./fuerte/include/fuerte)
#include_directories(./fuerte/include)
#include_directories(./fuerte)
#include_directories(./velocypack/include/velocypack)
#include_directories(./velocypack/include)
#include_directories(./velocypack)

#AUX_SOURCE_DIRECTORY(./lz4/lib/ SRC)
#AUX_SOURCE_DIRECTORY(./fuerte/src/ SRC)
#AUX_SOURCE_DIRECTORY(./velocypack/src/ SRC)
AUX_SOURCE_DIRECTORY(./velocypack/src SRC)

include_directories(/home/kapili3/arangodb/3rdParty/velocypack/include/)
include_directories(/home/kapili3/arangodb/3rdParty/fuerte/include/)

add_executable(untitled main.cpp ${SRC})
target_link_libraries(untitled
  Threads::Threads
  Boost::system
  Boost::thread
  ${OPENSSL_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}


#  /home/kapili3/arangodb/build/bin/libanalyzer-delimiter-s.a
#  /home/kapili3/arangodb/build/bin/libanalyzer-mask-s.a
#  /home/kapili3/arangodb/build/bin/libanalyzer-ngram-s.a
#  /home/kapili3/arangodb/build/bin/libanalyzer-norm-s.a
#  /home/kapili3/arangodb/build/bin/libanalyzer-stem-s.a
#  /home/kapili3/arangodb/build/bin/libanalyzer-text-s.a
#  /home/kapili3/arangodb/build/bin/libarango.a
#  /home/kapili3/arangodb/build/bin/libarango_agency.a
#  /home/kapili3/arangodb/build/bin/libarango_aql.a
#  /home/kapili3/arangodb/build/bin/libarango_cache.a
#  /home/kapili3/arangodb/build/bin/libarango_cluster_engine.a
#  /home/kapili3/arangodb/build/bin/libarango_cluster_methods.a
#  /home/kapili3/arangodb/build/bin/libarango_common_rest_handler.a
#  /home/kapili3/arangodb/build/bin/libarango_geo.a
#  /home/kapili3/arangodb/build/bin/libarango_graph.a
#  /home/kapili3/arangodb/build/bin/libarango_indexes.a
#  /home/kapili3/arangodb/build/bin/libarango_iresearch.a
#  /home/kapili3/arangodb/build/bin/libarango_mmfiles.a
#  /home/kapili3/arangodb/build/bin/libarango_network.a
#  /home/kapili3/arangodb/build/bin/libarango_pregel.a
#  /home/kapili3/arangodb/build/bin/libarango_replication.a
#  /home/kapili3/arangodb/build/bin/libarango_rocksdb.a
#  /home/kapili3/arangodb/build/bin/libarangoserver.a
#  /home/kapili3/arangodb/build/bin/libarango_storage_engine.a
#  /home/kapili3/arangodb/build/bin/libarango_storage_engine_common.a
#  /home/kapili3/arangodb/build/bin/libarango_utils.a
#  /home/kapili3/arangodb/build/bin/libarango_v8.a
#  /home/kapili3/arangodb/build/bin/libarango_v8server.a
#  /home/kapili3/arangodb/build/bin/libarango_vocbase.a
#  /home/kapili3/arangodb/build/bin/libboost_filesystem.a
#  /home/kapili3/arangodb/build/bin/libboost_locale.a
#  /home/kapili3/arangodb/build/bin/libboost_program_options.a
#  /home/kapili3/arangodb/build/bin/libboost_system.a
#  /home/kapili3/arangodb/build/bin/libboost_thread.a
#  /home/kapili3/arangodb/build/bin/libcurl.a
#  /home/kapili3/arangodb/build/bin/libformat-1_0-s.a
  /home/kapili3/arangodb/build/bin/libfuerte.a
#  /home/kapili3/arangodb/build/bin/libgtest.a
#  /home/kapili3/arangodb/build/bin/libiresearch-s.a
#  /home/kapili3/arangodb/build/bin/liblib_arango_shell.a
#  /home/kapili3/arangodb/build/bin/libllhttp.a
  /home/kapili3/arangodb/build/bin/liblz4.a
#  /home/kapili3/arangodb/build/bin/librocksdb.a
#  /home/kapili3/arangodb/build/bin/libs2.a
#  /home/kapili3/arangodb/build/bin/libscorer-bm25-s.a
#  /home/kapili3/arangodb/build/bin/libscorer-tfidf-s.a
#  /home/kapili3/arangodb/build/bin/libsnappy.a
#  /home/kapili3/arangodb/build/bin/libstemmer-static.a
#  /home/kapili3/arangodb/build/bin/libz.a

)
