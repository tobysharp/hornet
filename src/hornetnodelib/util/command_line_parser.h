// Copyright 2025 Toby Sharp
//
// Part of the Hornet Node project.
// Defines a declarative command-line parser that populates a config struct.
#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hornetlib/util/throw.h"

namespace hornet::node::util {

class CommandLineParser {
 public:
  CommandLineParser(std::string app_name, std::string version);

  template <typename T>
  void AddOption(const std::string& name, T* target, const std::string& help, T default_value = T{});

  void AddFlag(const std::string& name, bool* target, const std::string& help);

  // Parses argv and handles --help / --version internally.
  // Returns true if the application should proceed, false if help/version or an error occurs.
  bool Parse(int argc, char** argv);

  // Prints help text manually. Normally invoked automatically from Parse() on --help.
  void PrintHelp(std::ostream& out = std::cout, const std::string& header = "Options:") const;

 private:
  class OptionBase {
   public:
    virtual ~OptionBase() = default;
    virtual void Parse(const std::string& arg) = 0;
    virtual void Print(std::ostream& out) const = 0;
  };

  template <typename T>
  class Option : public OptionBase {
   public:
    Option(std::string name, T* target, std::string help, T default_value)
        : name_(std::move(name)), target_(target), help_(std::move(help)), default_(std::move(default_value)) {
      *target_ = default_;
    }

    virtual void Parse(const std::string& arg) override {
      std::istringstream ss{arg};
      T value;
      if (!(ss >> value)) {
        hornet::util::ThrowRuntimeError("Invalid value for option ", name_);
      }
      *target_ = value;
    }

    virtual void Print(std::ostream& out) const override {
      out << "  " << name_ << " (default: " << default_ << ")\n    " << help_ << "\n";
    }

   private:
    std::string name_;
    T* target_;
    std::string help_;
    T default_;
  };

  class Flag : public OptionBase {
   public:
    Flag(std::string name, bool* target, std::string help)
        : name_(std::move(name)), target_(target), help_(std::move(help)) {
      *target_ = false;
    }

    virtual void Parse(const std::string& /*arg*/) override {
      *target_ = true;
    }

    virtual void Print(std::ostream& out) const override {
      out << "  " << name_ << "\n    " << help_ << "\n";
    }

   private:
    std::string name_;
    bool* target_;
    std::string help_;
  };

  std::unordered_map<std::string, std::unique_ptr<OptionBase>> options_;
  std::string app_name_;
  std::string version_;
};

template <typename T>
void CommandLineParser::AddOption(const std::string& name, T* target, const std::string& help, T default_value) {
  options_[name] = std::make_unique<Option<T>>(name, target, help, default_value);
}

inline void CommandLineParser::AddFlag(const std::string& name, bool* target, const std::string& help) {
  options_[name] = std::make_unique<Flag>(name, target, help);
}

inline CommandLineParser::CommandLineParser(std::string app_name, std::string version)
    : app_name_(std::move(app_name)), version_(std::move(version)) {}

inline bool CommandLineParser::Parse(int argc, char** argv) {
  std::string header = app_name_ + " " + version_ + " command-line options:";
  try {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help" || arg == "-h") {
        PrintHelp(std::cout, header);
        return false;
      }
      if (arg == "--version") {
        std::cout << app_name_ << " " << version_ << "\n";
        return false;
      }

      auto eq = arg.find('=');
      std::string key = eq == std::string::npos ? arg : arg.substr(0, eq);
      std::string val = eq == std::string::npos ? "" : arg.substr(eq + 1);
      if (key.starts_with("--")) key = key.substr(2);       // remove "--"
      else if (key.starts_with("-")) key = key.substr(1);   // remove "-"

      auto it = options_.find(key);
      if (it == options_.end()) {
        std::cerr << "Unknown command-line option: " << key << "\n";
        PrintHelp(std::cerr, header);
        return false;
      }
      it->second->Parse(val);
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    PrintHelp(std::cerr, header);
    return false;
  }
  return true;
}

inline void CommandLineParser::PrintHelp(std::ostream& out, const std::string& header) const {
  out << header << "\n";
  out << "  --help, -h\n    Show this help message\n";
  out << "  --version\n    Show version information\n";
  for (const auto& [_, opt] : options_) {
    opt->Print(out);
  }
}

}  // namespace hornet::node::util
