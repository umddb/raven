#include "utils/catcache.h"
#include "catalog/translator/translator.h"

namespace ercat {

void test() {
    Config::init();
    if (!CatCache::init()) {
        return;
    }

    //initCatCache(&cat_cache);

    std::vector<std::string> input_str;
    input_str.emplace_back("SELECT r.name, a.name FROM Repo r, Asset a, Contains(r, a) WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT r.name, m.*, d FROM Contains(Repo r, Model m), Contains(r, DataSet d) WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT a.name FROM Asset a");
    input_str.emplace_back("SELECT r FROM Repo r");
    input_str.emplace_back("SELECT r FROM temp r");

    input_str.emplace_back("SELECT r, a FROM r, a, Contains(r, a) WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT r, m FROM Repo r, Model m WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT r, m FROM Contains(r, m)");
    input_str.emplace_back("SELECT r FROM Depends(Model m, Repo r)");

    Translator translator;
    for (auto& input : input_str) {
        std::cout << "Input: " << input << "\n";
        std::string output = translator.translate(input);
        const auto & errors = translator.errors();
        if (errors.empty()) {
            std::cout << "Output: " <<  output << "\n";
        }
        else {
            std::cout << "Errors: ";
            for (auto& error : errors) {
                std::cout << error << "\n";
            }
        }
        std::cout << "\n";
    }
}

}

int main() {
    ercat::test();
    return 0;
}