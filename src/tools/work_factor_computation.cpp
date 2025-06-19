#include <NTL/ZZ.h>
#include <atomic>
#include <binomials.hpp>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iomanip> // For std::setprecision
#include <iostream>
#include <isd_cost_estimate.hpp>
#include <nlohmann/json.hpp>
#include <omp.h>
#include <string>
// #include <logging.hpp>
#include <fmt/core.h>
#include <unordered_set>

#include "globals.hpp"
#include <cstring>
#include <sstream>
#include <vector>

#define NUM_BITS_REAL_MANTISSA 1024
#define IGNORE_DECODING_COST 0
// #define EXPLORE_REPRS

void to_json(nlohmann::json &j, const Result &r) {
  j = nlohmann::json{{"alg_name", r.alg_name},
                     {"params", r.params},
                     {"value", r.value},
                     {"gje_cost", r.gje_cost},
                     {"list_size", r.list_size}};
}

void from_json(const nlohmann::json &j, Result &r) {
  j.at("alg_name").get_to(r.alg_name);
  j.at("params").get_to(r.params);
  j.at("value").get_to(r.value);
  j.at("gje_cost").get_to(r.gje_cost);
}

std::unordered_set<Algorithm> parse_algorithms(const std::string& input) {
  std::unordered_set<Algorithm> selected_algorithms;
  std::stringstream ss(input);
  std::string token;

  while (std::getline(ss, token, ',')) {
    auto it = algorithm_map.find(token);
    if (it != algorithm_map.end()) {
      selected_algorithms.insert(it->second);
    } else {
      std::cerr << "Unknown algorithm: " << token << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  return selected_algorithms;
}

std::unordered_set<QuantumAlgorithm> parse_quantum_algorithms(const std::string& input) {
  std::unordered_set<QuantumAlgorithm> selected_algorithms;
  std::stringstream ss(input);
  std::string token;

  while (std::getline(ss, token, ',')) {
    auto it = quantum_algorithm_map.find(token);
    if (it != quantum_algorithm_map.end()) {
      selected_algorithms.insert(it->second);
    } else {
      std::cerr << "Unknown algorithm: " << token << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  return selected_algorithms;
}

QCAttackType parse_qc_attack_type(const std::string& input) {
  auto it = qc_attack_type_map.find(input);
  if (it != qc_attack_type_map.end()) {
    return it->second;
  } else {
    std::cerr << "Unknown QC attack type: " << input << std::endl;
    exit(EXIT_FAILURE);
  }
}

int handle_plain(const std::string args) {
  std::istringstream argStream(args);
  std::string token;
  std::vector<int> values;
  while (std::getline(argStream, token, ',')) {
    values.push_back(std::stoi(token));
  }

  if (values.size() != 4) {
    std::cerr << "Expected 4 comma-separated values, but got " << values.size()
              << std::endl;
    return 1;
  }

  int n = values[0];
  int k = values[1];
  int t = values[2];
  bool qc_block_size = values[3];

  for (int i = 0; i < static_cast<int>(Algorithm::Count); i++) {
    Algorithm algo = static_cast<Algorithm>(i);
    std::cout << "Algorithm " << algorithm_to_string(algo) << std::endl;
    Result current_c_res = c_isd_log_cost(n, k, t, qc_block_size,
                                          QCAttackType::Plain, false, {algo});
    std::cout << "Plain " << std::endl;
    std::cout << result_to_string(current_c_res) << std::endl;

    double red_fac;
    red_fac =
        get_qc_red_factor_classic_log(qc_block_size, n - k, QCAttackType::MRA);
    std::cout << "Classic MRA: " << current_c_res.value - red_fac << std::endl;
    red_fac =
        get_qc_red_factor_classic_log(qc_block_size, n - k, QCAttackType::KRA1);
    std::cout << "Classic KRA1: " << current_c_res.value - red_fac << std::endl;
    red_fac =
        get_qc_red_factor_classic_log(qc_block_size, n - k, QCAttackType::KRA2);
    std::cout << "Classic KRA2: " << current_c_res.value - red_fac << std::endl;
    red_fac =
        get_qc_red_factor_classic_log(qc_block_size, n - k, QCAttackType::KRA3);
    std::cout << "Classic KRA3: " << current_c_res.value - red_fac << std::endl;

    std::cout << "**********" << std::endl;
  }

  for (int i = 0; i < static_cast<int>(QuantumAlgorithm::Count); i++) {
    QuantumAlgorithm algo = static_cast<QuantumAlgorithm>(i);

    std::cout << "Algorithm: " << quantum_algorithm_to_string(algo)
              << std::endl;
    Result current_q_res =
        q_isd_log_cost(n, k, t, qc_block_size, QCAttackType::Plain, false,
                       std::unordered_set<QuantumAlgorithm>{algo});
    std::cout << "Plain " << std::endl;
    std::cout << result_to_string(current_q_res) << std::endl;

    double red_fac;
    red_fac =
        get_qc_red_factor_quantum_log(qc_block_size, n - k, QCAttackType::MRA);
    std::cout << "Quantum MRA: " << current_q_res.value - red_fac << std::endl;
  }
  return 0;
}

int handle_json(std::string json_filename, std::unordered_set<Algorithm> alg_list,
                std::unordered_set<QuantumAlgorithm> q_alg_list,
                QCAttackType qc_attack_type,
                std::string outDir) {
  std::ifstream file(json_filename);

  // Check if the file is open
  if (!file.is_open()) {
    std::cerr << "Could not open the input file " << json_filename << std::endl;
    return 1;
  }

  // Parse the JSON content
  nlohmann::json j;
  file >> j;

  int no_values = j.size();
  std::cout << "Number of values in the JSON: " << no_values << std::endl;
  std::filesystem::path dirPath =
    std::filesystem::path(outDir);
  // Check if the directory exists
  if (!std::filesystem::exists(dirPath)) {
    // Try to create the directory, including parent directories
    if (std::filesystem::create_directories(dirPath)) {
      std::cout << "Directory created successfully: " << outDir
                << std::endl;
    } else {
      std::cerr << "Failed to create directory: " << outDir
                << std::endl;
      return 1; // Return an error code
    }
  }

  // Define an atomic counter for processed entries
  std::atomic<int> processed_count(0);
  std::atomic<int> error_count(0);
  std::atomic<int> skipped_count(0);
  std::atomic<int> iteration(0);

  // Iterate over the list of entries. With schedule(dynamic) loop iterations
  // are divided into chunks, and threads dynamically grab chunks as they
  // complete their previous work.
#pragma omp parallel for schedule(dynamic)
  for (const auto &entry : j) {
    ++iteration;
    if (iteration % 1234 == 0) {
#pragma omp critical
      {
        std::cout << "\rProcessed: " << std::setw(8) << std::setfill(' ')
                  << processed_count << "; Skipped: " << std::setw(8)
                  << std::setfill(' ') << skipped_count
                  << "; Errors: " << std::setw(8) << std::setfill(' ')
                  << error_count << "; Remaining: " << std::setw(8)
                  << std::setfill(' ')
                  << (no_values - skipped_count - processed_count - error_count)
                  << " / " << no_values << std::flush;
      }
    }

    uint32_t n = entry["n"];
    // uint32_t r = entry["r"];
    // uint32_t k = n - r;
    uint32_t k = entry["k"];
    uint32_t r = n - k;
    uint32_t w = entry["w"];

    std::string filename = outDir +
                           fmt::format("{:06}_{:06}_{:03}.json", n, k, w);
    // Check if the generated file exists
    if (std::filesystem::exists(filename)) {
      // std::cout << "Generated file exists: " << filename << std::endl
      //           << ". Skipping.";
      ++skipped_count;
      continue;
    }
    // #pragma omp critical
    // std::cout << "Processing " << filename << std::endl;
    // uint32_t qc_block_size = entry["prime"];
    uint32_t qc_block_size = r;

    nlohmann::json out_values;

    Result current_c_res;
    Result current_q_res;

    if (!alg_list.empty()){
      current_c_res =
        c_isd_log_cost(n, k, w, qc_block_size, qc_attack_type, false,
                       alg_list);
      out_values["Classic"] = current_c_res;
    }

    if (!q_alg_list.empty()){
      current_q_res = q_isd_log_cost(
                                     n, k, w, qc_block_size, qc_attack_type, false,
                                     q_alg_list);
      out_values["Quantum"] = current_q_res;
    }

    // std::string attack_type;

    // Post-apply reduction factors
    // uint32_t n0 = n / r;
    // if (n0 == 0) {
    //   // It's a value with rate < .5; it happens for KRA2 attacks
    //   double red_fac =
    //       get_qc_red_factor_quantum_log(qc_block_size, n0,
    //       QCAttackType::MRA);
    //   out_values["Quantum"]["MRA"] = current_q_res.value - red_fac;

    //   red_fac =
    //       get_qc_red_factor_classic_log(qc_block_size, n0,
    //       QCAttackType::MRA);
    //   out_values["Classic"]["MRA"] = current_c_res.value - red_fac;

    //   red_fac =
    //       get_qc_red_factor_classic_log(qc_block_size, n0,
    //       QCAttackType::KRA1);
    //   out_values["Classic"]["KRA1"] = current_c_res.value - red_fac;
    //   red_fac =
    //       get_qc_red_factor_classic_log(qc_block_size, n0,
    //       QCAttackType::KRA2);
    //   out_values["Classic"]["KRA2"] = current_c_res.value - red_fac;
    //   red_fac =
    //       get_qc_red_factor_classic_log(qc_block_size, n0,
    //       QCAttackType::KRA3);
    //   out_values["Classic"]["KRA3"] = current_c_res.value - red_fac;
    // }

    std::ofstream file(filename);
    if (file.is_open()) {
      file << std::fixed << std::setprecision(10)
           << out_values.dump(4); // Format JSON with indentation
      file.close();
      ++processed_count;
      // std::cout << "Data written to " << filename << std::endl;
    } else {
      std::cerr << "Could not open the file!" << std::endl;
      ++error_count;
    }
  }

  std::cout << "\rProcessed: " << std::setw(8) << std::setfill(' ')
            << processed_count << "; Skipped: " << std::setw(8)
            << std::setfill(' ') << skipped_count
            << "; Errors: " << std::setw(8) << std::setfill(' ') << error_count
            << "; Remaining: " << std::setw(8) << std::setfill(' ')
            << (no_values - skipped_count - processed_count - error_count)
            << " / " << no_values << std::endl;

  return 0;
}

int main(int argc, char *argv[]) {
  // Logger::LoggerManager::getInstance().setup_logger(
  //     "binomials", spdlog::level::info, spdlog::level::debug);
  // Logger::LoggerManager::getInstance().setup_logger(
  //     "isd_cost_estimate", spdlog::level::info, spdlog::level::debug);
  std::string json_file, plain_args, alg_list_s, quantum_alg_list_s, qc_attack_type_s;
  std::string out_dir, out_suffix;
  bool json_mode = false, plain_mode = false;

  static struct option long_options[] = {
    {"json", required_argument, 0, 'j'},
    {"plain", required_argument, 0, 'p'},
    {"algorithms", required_argument, 0, 'a'},
    {"quantum-algorithms", required_argument, 0, 'q'},
    {"qc-attack-type", required_argument, 0, 't'},
    {"out-dir", required_argument, 0, 'd'},
    {"out", required_argument, 0, 'o'},
    {0, 0, 0, 0}
  };

  int option_index = 0;
  int c;
  while ((c = getopt_long(argc, argv, "j:p:a:q:t:d:o:", long_options, &option_index)) != -1) {
    switch (c) {
    case 'j': json_file = optarg; json_mode = true; break;
    case 'p': plain_args = optarg; plain_mode = true; break;
    case 'a': alg_list_s = optarg; break;
    case 'q': quantum_alg_list_s = optarg; break;
    case 't': qc_attack_type_s = optarg; break;
    case 'd': out_dir = optarg; break;
    case 'o': out_suffix = optarg; break;
    case '?': return 1;
    }
  }

  if (json_mode == plain_mode) {
    std::cerr << "Error: Specify exactly one of --json or --plain.\n";
    return 1;
  }

  if (qc_attack_type_s.empty() || out_dir.empty() || out_suffix.empty()) {
    std::cerr << "Error: --qc-attack-type, --out-dir, and --out are required.\n";
    return 1;
  }

  if (alg_list_s.empty() && quantum_alg_list_s.empty()) {
    std::cerr << "Error: At least one of --algorithms or --quantum-algorithms must be provided.\n";
    return 1;
  }

  auto algorithms = parse_algorithms(alg_list_s);
  auto qalgorithms = parse_quantum_algorithms(quantum_alg_list_s);
  auto qc_attack = parse_qc_attack_type(qc_attack_type_s);

  NTL::RR::SetPrecision(NUM_BITS_REAL_MANTISSA);
  InitBinomials();
  pi = NTL::ComputePi_RR();

  if (json_mode) {
    std::string output_path = out_dir + "/" + out_suffix + "/";
    handle_json(json_file, algorithms, qalgorithms, qc_attack, output_path);
  } else if (plain_mode) {
    // TODO not fully tested
    handle_plain(plain_args);
  } else {
    std::cerr << "Error: Unknown mode. Use either --json or --plain." << std::endl;
    return 1;
  }
}
