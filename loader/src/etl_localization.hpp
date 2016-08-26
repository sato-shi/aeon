/*
 Copyright 2016 Nervana Systems Inc.
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#pragma once

#include <vector>
#include <tuple>
#include <random>

#include "interface.hpp"
#include "etl_boundingbox.hpp"
#include "etl_image_full.hpp"
#include "util.hpp"
#include "box.hpp"

namespace nervana {

    namespace localization {
        class decoded;
        class params;
        class config;
        class target;
        class anchor;

        class extractor;
        class transformer;
        class loader;
    }

    class localization::target {
    public:
        target() : target(0,0,0,0){}
        target(float x, float y, float w, float h) :
            dx{x}, dy{y}, dw{w}, dh{h} {}
        float dx;
        float dy;
        float dw;
        float dh;
    };

    class localization::anchor {
    public:
        static std::vector<box> generate(const localization::config& cfg);
        static std::vector<int> inside_image_bounds(int width, int height, const std::vector<box>& all_anchors);
    private:
        anchor() = delete;

        //    Generate anchor (reference) windows by enumerating aspect ratios X
        //    scales wrt a reference (0, 0, 15, 15) window.
        static std::vector<box> generate_anchors(size_t base_size, const std::vector<float>& ratios, const std::vector<float>& scales);

        //    Enumerate a set of anchors for each aspect ratio wrt an anchor.
        static std::vector<box> ratio_enum(const box& anchor, const std::vector<float>& ratios);

        //    Given a vector of widths (ws) and heights (hs) around a center
        //    (x_ctr, y_ctr), output a set of anchors (windows).
        static std::vector<box> mkanchors(const std::vector<float>& ws, const std::vector<float>& hs, float x_ctr, float y_ctr);

        //    Enumerate a set of anchors for each scale wrt an anchor.
        static std::vector<box> scale_enum(const box& anchor, const std::vector<float>& scales);
    };

    class localization::config : public nervana::interface::config {
    public:
        size_t                      rois_per_image = 256;
        size_t                      min_size;   // copied from image_full config
        size_t                      max_size;   // copied from image_full config
        size_t                      base_size = 16;
        float                       scaling_factor = 1.0 / 16.;
        std::vector<float>          ratios = {0.5, 1, 2};
        std::vector<float>          scales = {8, 16, 32};
        float                       negative_overlap = 0.3;  // negative anchors have < 0.3 overlap with any gt box
        float                       positive_overlap = 0.7;  // positive anchors have > 0.7 overlap with at least one gt box
        float                       foreground_fraction = 0.5;  // at most, positive anchors are 0.5 of the total rois
        std::string                 type_string = "float";
        size_t                      max_gt_boxes = 64;
        std::vector<std::string>    labels;

        // Derived values
        size_t output_buffer_size;
        std::unordered_map<std::string,int> label_map;

        config(nlohmann::json js, const image_full::config& iconfig);

        size_t total_anchors() const
        {
            return ratios.size() * scales.size() * (int)pow(int(std::floor(max_size * scaling_factor)),2);
        }

    private:
        std::vector<std::shared_ptr<interface::config_info_interface>> config_list = {
            ADD_SCALAR(rois_per_image, mode::OPTIONAL),
            ADD_SCALAR(base_size, mode::OPTIONAL),
            ADD_SCALAR(scaling_factor, mode::OPTIONAL),
            ADD_SCALAR(ratios, mode::OPTIONAL),
            ADD_SCALAR(scales, mode::OPTIONAL),
            ADD_SCALAR(negative_overlap, mode::OPTIONAL, [](float v){ return v>=0.0 && v <=1.0; }),
            ADD_SCALAR(positive_overlap, mode::OPTIONAL, [](float v){ return v>=0.0 && v <=1.0; }),
            ADD_SCALAR(foreground_fraction, mode::OPTIONAL, [](float v){ return v>=0.0 && v <=1.0; }),
            ADD_SCALAR(type_string, mode::OPTIONAL, [](const std::string& v){ return output_type::is_valid_type(v); }),
            ADD_SCALAR(max_gt_boxes, mode::OPTIONAL),
            ADD_SCALAR(labels, mode::REQUIRED)
        };

        config() {}
        void validate();
    };

    class localization::decoded : public boundingbox::decoded {
    public:
        decoded() {}
        virtual ~decoded() override {}

        // from transformer
        std::vector<int>                labels;
        std::vector<target>             bbox_targets;
        std::vector<int>                anchor_index;
        float                           image_scale;
        cv::Size                        output_image_size;
        std::vector<boundingbox::box>   gt_boxes;
    };

    class localization::extractor : public nervana::interface::extractor<localization::decoded> {
    public:
        extractor(const localization::config&);

        virtual std::shared_ptr<localization::decoded> extract(const char* data, int size) override
        {
            auto rc = std::make_shared<localization::decoded>();
            auto bb = std::static_pointer_cast<boundingbox::decoded>(rc);
            bbox_extractor.extract(data, size, bb);
            if(!bb) rc = nullptr;
            return rc;
        }

        virtual ~extractor() {}
    private:
        extractor() = delete;
        boundingbox::extractor bbox_extractor;
    };

    class localization::transformer : public interface::transformer<localization::decoded, image_full::params> {
    public:
        transformer(const localization::config&);

        virtual ~transformer() {}

        std::shared_ptr<localization::decoded> transform(
                            std::shared_ptr<image_full::params> txs,
                            std::shared_ptr<localization::decoded> mp) override;
    private:
        transformer() = delete;
        cv::Mat bbox_overlaps(const std::vector<box>& boxes, const std::vector<boundingbox::box>& query_boxes);
        static std::vector<target> compute_targets(const std::vector<box>& gt_bb, const std::vector<box>& anchors);
        std::vector<int> sample_anchors(const std::vector<int>& labels, bool debug=false);

        const localization::config& cfg;
        std::minstd_rand0           random;
        const std::vector<box>      all_anchors;
    };

    class localization::loader : public interface::loader<localization::decoded> {
    public:
        loader(const localization::config&);

        virtual ~loader() {}

        void load(const std::vector<void*>& buf_list, std::shared_ptr<localization::decoded> mp) override;
    private:
        loader() = delete;
        int                     total_anchors;
        size_t                  max_gt_boxes;
        std::vector<shape_type> shape_type_list;
    };
}
