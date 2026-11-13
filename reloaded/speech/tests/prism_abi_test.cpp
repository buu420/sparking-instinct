#include "prism_api.h"

#include <cstdlib>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        std::cerr << "Expected the path to the pinned x64 prism.dll\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path path(argv[1]);
    sparking::speech::PrismApi missing(path.parent_path() / L"missing-prism.dll");
    if (missing || !missing.error()) {
        std::cerr << "A missing dependency was not rejected\n";
        return EXIT_FAILURE;
    }

    sparking::speech::PrismApi api(path);
    if (!api) {
        std::cerr << api.error() << '\n';
        return EXIT_FAILURE;
    }

    auto config = api.make_config();
    if (!config || config->version != PRISM_CONFIG_VERSION || config->availability_callback ||
        config->availability_baseline_callback || config->registry || config->availability_userdata) {
        std::cerr << "PrismConfig did not round-trip with the pinned header\n";
        return EXIT_FAILURE;
    }
    auto* context = api.init(&*config);
    if (!context) {
        std::cerr << "Prism returned no context\n";
        return EXIT_FAILURE;
    }
    const auto backends = api.registry_count(context);
    api.shutdown(context);
    if (!backends) {
        std::cerr << "Prism has no built-in backends\n";
        return EXIT_FAILURE;
    }

    std::cout << "Prism " << PRISM_VERSION_STRING << " config ABI "
              << static_cast<unsigned>(config->version) << " (" << sizeof(PrismConfig)
              << " bytes), " << backends << " registered backends, clean shutdown\n";
}
