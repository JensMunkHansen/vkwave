#include "cli.h"
#include "app_config.h"

#include <args.hxx>

bool parse_cli(int argc, char** argv, AppConfig& config, std::string& config_path)
{
  args::ArgumentParser parser("vkwave -- async GPU rendering engine");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::CompletionFlag complete(parser, {"complete"});

  args::ValueFlag<std::string> config_flag(
    parser, "path", "Path to config file (default: vkwave.toml)", {'c', "config"});
  args::ValueFlag<uint64_t> max_frames(
    parser, "N", "Exit after N frames (0 = unlimited)", {"max-frames"});
  args::ValueFlag<std::string> present_mode(
    parser, "mode", "Present mode: immediate, mailbox, fifo, fifo_relaxed", {"present-mode"});

  try
  {
    parser.ParseCLI(argc, argv);
  }
  catch (const args::Completion& e)
  {
    std::cout << e.what();
    return false;
  }
  catch (const args::Help&)
  {
    std::cout << parser;
    return false;
  }
  catch (const args::ParseError& e)
  {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return false;
  }

  if (config_flag)
    config_path = args::get(config_flag);
  if (max_frames)
    config.max_frames = args::get(max_frames);
  if (present_mode)
    config.present_mode = args::get(present_mode);

  return true;
}
