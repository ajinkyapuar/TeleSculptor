/*ckwg +29
 * Copyright 2017 by Kitware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name Kitware, Inc. nor the names of any contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "TrackFeaturesSprokitTool.h"

#include <fstream>
#include <sstream>
#include <maptk/colorize.h>
#include <maptk/version.h>

#include <vital/algo/image_io.h>
#include <vital/algo/convert_image.h>
#include <vital/algo/track_features.h>
#include <vital/algo/video_input.h>

#include <vital/config/config_block_io.h>
#include <vital/types/metadata.h>
#include <vital/types/metadata_traits.h>

#include <qtStlUtil.h>

#include <QtGui/QApplication>
#include <QtGui/QMessageBox>

#include <QtCore/QDir>

#include <sprokit/pipeline/pipeline.h>
#include <sprokit/processes/kwiver_type_traits.h>
#include <sprokit/processes/adapters/embedded_pipeline.h>
#include <sprokit/pipeline_util/literal_pipeline.h>

#include <kwiversys/SystemTools.hxx>

using kwiver::vital::algo::image_io;
using kwiver::vital::algo::image_io_sptr;
using kwiver::vital::algo::convert_image;
using kwiver::vital::algo::convert_image_sptr;
using kwiver::vital::algo::track_features;
using kwiver::vital::algo::track_features_sptr;
using kwiver::vital::algo::video_input;
using kwiver::vital::algo::video_input_sptr;

namespace
{
  static char const* const BLOCK_CI = "image_converter";
  static char const* const BLOCK_TF = "feature_tracker";
  static char const* const BLOCK_VR = "video_reader";

  //-----------------------------------------------------------------------------
  kwiver::vital::config_block_sptr readConfig(std::string const& name)
  {
    try
    {
      using kwiver::vital::read_config_file;

      auto const exeDir = QDir(QApplication::applicationDirPath());
      auto const prefix = stdString(exeDir.absoluteFilePath(".."));
      return read_config_file(name, "maptk", MAPTK_VERSION, prefix);
    }
    catch (...)
    {
      return{};
    }
  }
}  // end anonymous namespace


//-----------------------------------------------------------------------------
class TrackFeaturesSprokitToolPrivate
{
public:
  TrackFeaturesSprokitToolPrivate();
  convert_image_sptr image_converter;
  track_features_sptr feature_tracker;
  video_input_sptr video_reader;
};

TrackFeaturesSprokitToolPrivate
::TrackFeaturesSprokitToolPrivate()
{

}

QTE_IMPLEMENT_D_FUNC(TrackFeaturesSprokitTool)

//-----------------------------------------------------------------------------
TrackFeaturesSprokitTool::TrackFeaturesSprokitTool(QObject* parent)
  : AbstractTool(parent), d_ptr(new TrackFeaturesSprokitToolPrivate)
{
  this->setText("&Track Features");
  this->setToolTip(
    "<nobr>Track features through a the video and identify keyframes. "
    "</nobr>Compute descriptors on the keyframes and match to a visual "
    "index to close loops.");
}

//-----------------------------------------------------------------------------
TrackFeaturesSprokitTool::~TrackFeaturesSprokitTool()
{
}

//-----------------------------------------------------------------------------
AbstractTool::Outputs TrackFeaturesSprokitTool::outputs() const
{
  return Tracks | ActiveFrame;
}

//-----------------------------------------------------------------------------
bool TrackFeaturesSprokitTool::execute(QWidget* window)
{
  QTE_D();

  if (!this->hasVideoSource())
  {
    QMessageBox::information(
      window, "Insufficient data", "This operation requires a video source.");
    return false;
  }

  // Load configuration
  auto const config = readConfig("gui_track_features.conf");

  // Check configuration
  if (!config)
  {
    QMessageBox::critical(
      window, "Configuration error",
      "No configuration data was found. Please check your installation.");
    return false;
  }

  if (!convert_image::check_nested_algo_configuration(BLOCK_CI, config) ||
      !video_input::check_nested_algo_configuration(BLOCK_VR, this->data()->config))
  {
    QMessageBox::critical(
      window, "Configuration error",
      "An error was found in the algorithm configuration.");
    return false;
  }

  // Create algorithm from configuration
  convert_image::set_nested_algo_configuration(BLOCK_CI, config, d->image_converter);
  video_input::set_nested_algo_configuration(BLOCK_VR, this->data()->config, d->video_reader);

  return AbstractTool::execute(window);
}

std::stringstream
TrackFeaturesSprokitTool
::create_pipeline_config()
{
  std::stringstream ss;

  bool from_file = false;
  if (from_file)
  {
    std::string pipe_file = "D:/export_controlled_data/telesculptor/sequence2/track_features_embedded.pipe";  //TODO THIS NEEDS TO POINT TO THE BUILD OR INSTALL DIR COPY

                                                                                                              // Open pipeline description
    std::ifstream pipe_str;
    pipe_str.open(pipe_file, std::ifstream::in);

    if (!pipe_str)
    {
      return ss;
    }
    ss << pipe_str.rdbuf();
    return ss;
  }
  else
  {
    std::string voc_path;
    kwiver::vital::config_path_list_t kwiver_config_path;
    kwiversys::SystemTools::GetPath(kwiver_config_path, "KWIVER_CONFIG_PATH");
    if (!kwiver_config_path.empty())
    {
      //look in the kwiver config directory
      voc_path = kwiver_config_path[0] + "/kwiver_voc.yml.gz";
    }
    else
    {
      //look in the working directory
      voc_path = "kwiver_voc.yml.gz";
    }

    ss << SPROKIT_PROCESS("input_adapter", "input")

      << SPROKIT_PROCESS("feature_tracker", "tracker")
      << SPROKIT_CONFIG("track_features:type", "ocv_KLT")
      << SPROKIT_CONFIG("track_features:ocv_KLT:new_feat_exclusionary_radius_image_fraction", "0.02")
      << SPROKIT_CONFIG("track_features:ocv_KLT:redetect_frac_lost_threshold", "0.7")
      << SPROKIT_CONFIG("track_features:ocv_KLT:feature_detector:type", "ocv_FAST")
      << SPROKIT_CONFIG("track_features:ocv_KLT:feature_detector:ocv_FAST:threshold", "50")
      << SPROKIT_CONFIG("track_features:ocv_KLT:feature_detector:ocv_FAST:nonmaxSuppression", "true")

      << SPROKIT_PROCESS("keyframe_selection_process", "keyframes")
      << SPROKIT_CONFIG("keyframe_selection_1:type", "basic")
      << SPROKIT_CONFIG("keyframe_selection_1:basic:fraction_tracks_lost_to_necessitate_new_keyframe", "0.1")

      << SPROKIT_PROCESS("detect_features_if_keyframe_process", "detect_if_keyframe")
      << SPROKIT_CONFIG("augment_keyframes:type","augment_keyframes")
      << SPROKIT_CONFIG("augment_keyframes:augment_keyframes:kf_only_feature_detector:type", "ocv_ORB")
      << SPROKIT_CONFIG("augment_keyframes:augment_keyframes:kf_only_descriptor_extractor:type", "ocv_ORB")
      << SPROKIT_CONFIG("augment_keyframes:augment_keyframes:kf_only_feature_detector:ocv_ORB:n_features", "2000")

      << SPROKIT_PROCESS("close_loops_process", "loop_detector")
      << SPROKIT_CONFIG("close_loops:type", "appearance_indexed")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:min_loop_inlier_matches", "50")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:match_features:type", "homography_guided")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:match_features:homography_guided:homography_estimator:type", "ocv")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:match_features:homography_guided:feature_matcher1:type", "ocv_brute_force")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:type", "dbow2")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:feature_detector:type", "ocv_ORB")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:descriptor_extractor:type", "ocv_ORB")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:image_io:type", "ocv")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:max_num_candidate_matches_from_vocabulary_tree", "20")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:training_image_list_path", "")
      << SPROKIT_CONFIG("close_loops:appearance_indexed:bag_of_words_matching:dbow2:vocabulary_path", voc_path)

      << SPROKIT_PROCESS("draw_tracks", "draw")
      << SPROKIT_CONFIG("draw_tracks:type", "ocv")
      << SPROKIT_CONFIG("draw_tracks:ocv:draw_track_ids", "false")
      << SPROKIT_CONFIG("draw_tracks:ocv:draw_untracked_features", "true")
      << SPROKIT_CONFIG("draw_tracks:ocv:draw_shift_lines", "false")
      << SPROKIT_CONFIG("draw_tracks:ocv:draw_match_lines", "true")

      << SPROKIT_PROCESS("image_viewer", "disp")
      << SPROKIT_CONFIG("annotate_image", "true")
      << SPROKIT_CONFIG("pause_time", "0.001")
      << SPROKIT_CONFIG("title", "images")

      << SPROKIT_PROCESS("output_adapter", "output")
      << SPROKIT_CONFIG_BLOCK("_pipeline:_edge")
      << SPROKIT_CONFIG("capacity", "2")

      << SPROKIT_CONNECT("input", "image", "tracker", "image")
      << SPROKIT_CONNECT("input", "timestamp", "tracker", "timestamp")
      << SPROKIT_CONNECT("tracker", "feature_track_set", "tracker", "feature_track_set")
      << SPROKIT_CONNECT("tracker", "feature_track_set", "keyframes", "next_tracks")
      << SPROKIT_CONNECT("input", "timestamp", "keyframes", "timestamp")
      << SPROKIT_CONNECT("keyframes", "feature_track_set", "keyframes", "loop_back_tracks")
      << SPROKIT_CONNECT("keyframes", "feature_track_set", "detect_if_keyframe", "next_tracks")
      << SPROKIT_CONNECT("detect_if_keyframe", "feature_track_set", "detect_if_keyframe", "loop_back_tracks")
      << SPROKIT_CONNECT("input", "image", "detect_if_keyframe", "image")
      << SPROKIT_CONNECT("input", "timestamp", "detect_if_keyframe", "timestamp")
      << SPROKIT_CONNECT("detect_if_keyframe", "feature_track_set", "draw", "feature_track_set")
      << SPROKIT_CONNECT("detect_if_keyframe", "feature_track_set", "loop_detector", "next_tracks")
      << SPROKIT_CONNECT("loop_detector", "feature_track_set", "loop_detector", "loop_back_tracks")
      << SPROKIT_CONNECT("input", "timestamp", "loop_detector", "timestamp")
      << SPROKIT_CONNECT("input", "image", "draw", "image")
      << SPROKIT_CONNECT("input", "timestamp", "disp", "timestamp")
      << SPROKIT_CONNECT("draw", "output_image", "disp", "image")
      << SPROKIT_CONNECT("loop_detector", "feature_track_set", "output", "feature_track_set");

    return ss;
  }
}

//-----------------------------------------------------------------------------
void
TrackFeaturesSprokitTool
::run()
{
  QTE_D();

  std::stringstream pipe_str = create_pipeline_config();
  if (pipe_str.str().empty())
  {
    return;
  }

  // create a embedded pipeline
  kwiver::embedded_pipeline ep;
  ep.build_pipeline(pipe_str);

  // Start pipeline and wait for it to finish
  ep.start();

  unsigned int frame = this->activeFrame();
  kwiver::vital::timestamp currentTimestamp;

  d->video_reader->open(this->data()->videoPath);

  // Seek to just before active frame TODO: check status?
  if (frame > 1)
  {
    d->video_reader->seek_frame(currentTimestamp, frame - 1);
  }

  while (d->video_reader->next_frame(currentTimestamp))
  {
    auto const image = d->video_reader->frame_image();
    auto const converted_image = d->image_converter->convert(image);

    auto const mdv = d->video_reader->frame_metadata();
    if (!mdv.empty())
    {
      converted_image->set_metadata(mdv[0]);
    }

    // Create dataset for input
    auto ds = kwiver::adapter::adapter_data_set::create();
    ds->add_value("image", converted_image);
    ds->add_value("timestamp", currentTimestamp);
    ep.send(ds);

    if (this->isCanceled())
    {
      break;
    }
    if (!ep.empty())
    {
      auto rds = ep.receive();
    }
  }
  ep.send_end_of_input();

  kwiver::vital::feature_track_set_sptr out_tracks;
  while (!ep.at_end())
  {
    auto rds = ep.receive();
    auto ix = rds->find("feature_track_set");
    if (ix != rds->end())
    {
      out_tracks = ix->second->get_datum<kwiver::vital::feature_track_set_sptr>();
    }
  }
  ep.wait();

  this->updateTracks(out_tracks);

}
