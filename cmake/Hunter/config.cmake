hunter_config(
    Boost
    VERSION 1.70.0-p0
)

hunter_config(GSL
    URL https://github.com/microsoft/GSL/archive/v2.0.0.tar.gz
    SHA1 9bbdea551b38d7d09ab7aa2e89b91a66dd032b4a
    CMAKE_ARGS GSL_TEST=OFF
    )

hunter_config(
    GTest
    VERSION 1.10.0
    CMAKE_ARGS "CMAKE_CXX_FLAGS=-Wno-deprecated"
)

hunter_config(
    spdlog
    URL https://github.com/hunter-packages/spdlog/archive/v1.4.2-p0.tar.gz
    SHA1 61136ee6120fe069d37df4ad11628a2a0622b447
)

hunter_config(
    tsl_hat_trie
    URL https://github.com/masterjedy/hat-trie/archive/343e0dac54fc8491065e8a059a02db9a2b1248ab.zip
    SHA1 7b0051e9388d629f382752dd6a12aa8918cdc022
)

hunter_config(
    Boost.DI
    URL https://github.com/masterjedy/di/archive/c5287ee710ad90f5286d0cc2b9e49b72d89267a6.zip
    SHA1 802b64a6242be45771f3d4c86257eac0a3c7b289
    # disable building examples and tests, disable testing
    CMAKE_ARGS BOOST_DI_OPT_BUILD_TESTS=OFF BOOST_DI_OPT_BUILD_EXAMPLES=OFF
)
