/**
 * @file pipeline.hpp
 * @brief Run parsed operations on one or more input files.
 */
#pragma once

#include "vidwizard/cli.hpp"

namespace vidwizard
{

/**
 * @brief Process every expanded input according to @p opt.
 *
 * @param[in] opt Fully parsed options (jobs already resolved).
 *
 * @retval 0  All inputs succeeded.
 * @retval 1  Argument/operation error (no files processed).
 * @retval 2  At least one input failed.
 */
int run_pipeline(const cli_options &opt);

/**
 * @brief Process a single input file.
 *
 * @param[in] opt   Options.
 * @param[in] input Existing video file.
 * @param[in] input_count Total expanded inputs (naming).
 *
 * @return 0 on success, non-zero on failure.
 */
int process_one(const cli_options &opt, const std::filesystem::path &input,
                std::size_t input_count);

} // namespace vidwizard
