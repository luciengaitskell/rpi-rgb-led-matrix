// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2018 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "pixel-mapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>

namespace rgb_matrix {
namespace {

class RotatePixelMapper : public PixelMapper {
public:
  RotatePixelMapper() : angle_(0) {}

  virtual const char *GetName() const { return "Rotate"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (param == NULL || strlen(param) == 0) {
      angle_ = 0;
      return true;
    }
    char *errpos;
    const int angle = strtol(param, &errpos, 10);
    if (*errpos != '\0') {
      fprintf(stderr, "Invalid rotate parameter '%s'\n", param);
      return false;
    }
    if (angle % 90 != 0) {
      fprintf(stderr, "Rotation needs to be multiple of 90 degrees\n");
      return false;
    }
    angle_ = (angle + 360) % 360;
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    if (angle_ % 180 == 0) {
      *visible_width = matrix_width;
      *visible_height = matrix_height;
    } else {
      *visible_width = matrix_height;
      *visible_height = matrix_width;
    }
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    switch (angle_) {
    case 0:
      *matrix_x = x;
      *matrix_y = y;
      break;
    case 90:
      *matrix_x = matrix_width - y - 1;
      *matrix_y = x;
      break;
    case 180:
      *matrix_x = matrix_width - x - 1;
      *matrix_y = matrix_height - y - 1;
      break;
    case 270:
      *matrix_x = y;
      *matrix_y = matrix_height - x - 1;
      break;
    }
  }

private:
  int angle_;
};

class MirrorPixelMapper : public PixelMapper {
public:
  MirrorPixelMapper() : horizontal_(true) {}

  virtual const char *GetName() const { return "Mirror"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (param == NULL || strlen(param) == 0) {
      horizontal_ = true;
      return true;
    }
    if (strlen(param) != 1) {
      fprintf(stderr, "Mirror parameter should be a single "
              "character:'V' or 'H'\n");
    }
    switch (*param) {
    case 'V':
    case 'v':
      horizontal_ = false;
      break;
    case 'H':
    case 'h':
      horizontal_ = true;
      break;
    default:
      fprintf(stderr, "Mirror parameter should be either 'V' or 'H'\n");
      return false;
    }
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_height = matrix_height;
    *visible_width = matrix_width;
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    if (horizontal_) {
      *matrix_x = matrix_width - 1 - x;
      *matrix_y = y;
    } else {
      *matrix_x = x;
      *matrix_y = matrix_height - 1 - y;
    }
  }

private:
  bool horizontal_;
};

// If we take a long chain of panels and arrange them in a U-shape, so
// that after half the panels we bend around and continue below. This way
// we have a panel that has double the height but only uses one chain.
// A single chain display with four 32x32 panels can then be arranged in this
// 64x64 display:
//    [<][<][<][<] }- Raspbery Pi connector
//
// can be arranged in this U-shape
//    [<][<] }----- Raspberry Pi connector
//    [>][>]
//
// This works for more than one chain as well. Here an arrangement with
// two chains with 8 panels each
//   [<][<][<][<]  }-- Pi connector #1
//   [>][>][>][>]
//   [<][<][<][<]  }--- Pi connector #2
//   [>][>][>][>]
class UArrangementMapper : public PixelMapper {
public:
  UArrangementMapper() : parallel_(1) {}

  virtual const char *GetName() const { return "U-mapper"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (chain < 2) {  // technically, a chain of 2 would work, but somewhat pointless
      fprintf(stderr, "U-mapper: need at least --led-chain=4 for useful folding\n");
      return false;
    }
    if (chain % 2 != 0) {
      fprintf(stderr, "U-mapper: Chain (--led-chain) needs to be divisible by two\n");
      return false;
    }
    parallel_ = parallel;
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_width = (matrix_width / 64) * 32;   // Div at 32px boundary
    *visible_height = 2 * matrix_height;
    if (matrix_height % parallel_ != 0) {
      fprintf(stderr, "%s For parallel=%d we would expect the height=%d "
              "to be divisible by %d ??\n",
              GetName(), parallel_, matrix_height, parallel_);
      return false;
    }
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    const int panel_height = matrix_height / parallel_;
    const int visible_width = (matrix_width / 64) * 32;
    const int slab_height = 2 * panel_height;   // one folded u-shape
    const int base_y = (y / slab_height) * panel_height;
    y %= slab_height;
    if (y < panel_height) {
      x += matrix_width / 2;
    } else {
      x = visible_width - x - 1;
      y = slab_height - y - 1;
    }
    *matrix_x = x;
    *matrix_y = base_y + y;
  }

private:
  int parallel_;
};



class VerticalMapper : public PixelMapper {
public:
  VerticalMapper() {}

  virtual const char *GetName() const { return "V-mapper"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    chain_ = chain;
    parallel_ = parallel;
    // optional argument :Z allow for every other panel to be flipped
    // upside down so that cabling can be shorter:
    // [ O < I ]   without Z       [ O < I  ]
    //   ,---^      <----                ^
    // [ O < I ]                   [ I > O  ]
    //   ,---^            with Z     ^
    // [ O < I ]            --->   [ O < I  ]
    z_ = (param && strcasecmp(param, "Z") == 0);
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_width = matrix_width * parallel_ / chain_;
    *visible_height = matrix_height * chain_ / parallel_;
#if 0
     fprintf(stderr, "%s: C:%d P:%d. Turning W:%d H:%d Physical "
	     "into W:%d H:%d Virtual\n",
             GetName(), chain_, parallel_,
	     *visible_width, *visible_height, matrix_width, matrix_height);
#endif
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    const int panel_width  = matrix_width  / chain_;
    const int panel_height = matrix_height / parallel_;
    // because the panel you plug into ends up being the "bottom" panel and coordinates
    // start from the top panel, and you typically don't wire the bottom panel (first in
    // the chain) upside down, whether each panel gets swapped depends on this.
    // Without this, if you wire for 4 panels high and add a 5h panel, without this
    // code everything would get reversed and you'd have to re-layout all the panels
    bool is_height_even_panels = ( matrix_width / panel_width) % 2;
    const int x_panel_start = y / panel_height * panel_width;
    const int y_panel_start = x / panel_width * panel_height;
    const int x_within_panel = x % panel_width;
    const int y_within_panel = y % panel_height;
    const bool needs_flipping = z_ && (is_height_even_panels - ((y / panel_height) % 2)) == 0;
    *matrix_x = x_panel_start + (needs_flipping
                                 ? panel_width - 1 - x_within_panel
                                 : x_within_panel);
    *matrix_y = y_panel_start + (needs_flipping
                                 ? panel_height - 1 - y_within_panel
                                 : y_within_panel);
  }

private:
  bool z_;
  int chain_;
  int parallel_;
};

// Windmill mapper: arrange two parallel chains that start at the center and
// extend outward to left and right. Panels are mounted in portrait (e.g. 32x64)
// and the overall display is assembled horizontally to height=rows*parallel
// and width=rows*parallel*chain (each portrait panel contributes 'rows' pixels
// to the width when rotated). This mapper keeps the final logical size as
// width = panel_height * chain * parallel; height = panel_width.
//
// Parameters (optional):
//  - "Z"  : flip every other panel in each chain (serpentine cabling)
//  - "S"  : swap left/right chains (if your parallel wiring is reversed)
class WindmillPixelMapper : public PixelMapper {
public:
  WindmillPixelMapper() : z_(false), swap_lr_(false), chain_(1), parallel_(1) {}

  virtual const char *GetName() const { return "Windmill"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    chain_ = chain;
    parallel_ = parallel;
    if (parallel_ != 2) {
      fprintf(stderr, "Windmill: requires --led-parallel=2 (got %d)\n",
              parallel_);
      return false;
    }
    z_ = false;
    swap_lr_ = false;
    if (param && *param) {
      for (const char *p = param; *p; ++p) {
        const char c = *p;
        if (c == ':' || c == ',' || c == ';' || c == ' ')
          continue; // ignore separators
        switch (c) {
        case 'Z':
        case 'z':
          z_ = true;
          break;
        case 'S':
        case 's':
          swap_lr_ = true;
          break;
        default:
          fprintf(stderr, "Windmill: unknown parameter '%c' (use Z and/or S)\n",
                  c);
          return false;
        }
      }
    }
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height) const {
    const int panel_width = matrix_width / chain_;      // e.g. 64
    const int panel_height = matrix_height / parallel_; // e.g. 32
    // Each portrait panel contributes panel_height pixels to the final width,
    // and final height equals panel_width.
    *visible_width = panel_height * chain_ * parallel_;
    *visible_height = panel_width;
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height, int x,
                                  int y, int *matrix_x, int *matrix_y) const {
    const int panel_width = matrix_width / chain_;      // physical panel width
    const int panel_height = matrix_height / parallel_; // physical panel height

    // Compute which rotated panel we are in along the logical width.
    const int panels_total =
        chain_ * parallel_; // total panels across final width
    const int panel_index_along_width = x / panel_height; // 0..panels_total-1
    const int rx =
        x % panel_height; // within rotated panel (width = panel_height)
    const int ry = y;     // within rotated panel (height = panel_width)

    // Map logical panel index to (parallel channel p, position along chain
    // cpos) Left half consists of the chain that extends to the left from
    // center. Right half consists of the chain that extends to the right from
    // center.
    const int half = chain_;
    const int is_left_half = (panel_index_along_width < half) ? 1 : 0;
    const int idx_in_half = is_left_half ? (half - 1 - panel_index_along_width)
                                         : (panel_index_along_width - half);

    // Choose which parallel channel maps to left/right. By default, p_left=0,
    // p_right=1
    const int p_left = swap_lr_ ? 1 : 0;
    const int p_right = swap_lr_ ? 0 : 1;
    const int p = is_left_half ? p_left : p_right; // 0 or 1
    // Left half: map from far left (x=0) toward center as cpos increases.
    // Right half: keep previous fix so scanning from center to far right
    // progresses correctly.
    const int cpos =
        is_left_half ? panel_index_along_width : (chain_ - 1 - idx_in_half);

    // Rotate the portrait panel by 90 degrees to achieve 64px height.
    // We'll choose a rotation such that the top of the final display (y small)
    // maps to the top-row of the physical panel after rotation.
    // Using CCW rotation: (ux,uy) from (rx,ry)
    int ux = ry; // within physical panel width (0..panel_width-1)
    int uy = (panel_height - 1 -
              rx); // within physical panel height (0..panel_height-1)

    // For the left half, the observed orientation needs a vertical flip so that
    // y grows downward (top-left origin for the full display).
    if (is_left_half) {
      uy = panel_height - 1 - uy;
  }

  // Optional serpentine flip every other panel in each chain.
  if (z_ && (cpos % 2 == 1)) {
    ux = panel_width - 1 - ux;
    uy = panel_height - 1 - uy;
    }

    // Compose final physical matrix coordinates.
    *matrix_x = cpos * panel_width + ux;
    *matrix_y = p * panel_height + uy;

    // quick cludge fix
    // the left half of the windmill is flipped by 180 and needs to be corrected
    // make sure to re-evaluate is_left_half

    if (is_left_half) {
      *matrix_x = (cpos + 1) * panel_width - 1 - ux;
      *matrix_y = (p + 1) * panel_height - 1 - uy;
    }
  }

private:
  bool z_;
  bool swap_lr_;
  int chain_;
  int parallel_;
};

typedef std::map<std::string, PixelMapper*> MapperByName;
static void RegisterPixelMapperInternal(MapperByName *registry,
                                        PixelMapper *mapper) {
  assert(mapper != NULL);
  std::string lower_name;
  for (const char *n = mapper->GetName(); *n; n++)
    lower_name.append(1, tolower(*n));
  (*registry)[lower_name] = mapper;
}

static MapperByName *CreateMapperMap() {
  MapperByName *result = new MapperByName();

  // Register all the default PixelMappers here.
  RegisterPixelMapperInternal(result, new RotatePixelMapper());
  RegisterPixelMapperInternal(result, new UArrangementMapper());
  RegisterPixelMapperInternal(result, new VerticalMapper());
  RegisterPixelMapperInternal(result, new WindmillPixelMapper());
  RegisterPixelMapperInternal(result, new MirrorPixelMapper());
  return result;
}

static MapperByName *GetMapperMap() {
  static MapperByName *singleton_instance = CreateMapperMap();
  return singleton_instance;
}
}  // anonymous namespace

// Public API.
void RegisterPixelMapper(PixelMapper *mapper) {
  RegisterPixelMapperInternal(GetMapperMap(), mapper);
}

std::vector<std::string> GetAvailablePixelMappers() {
  std::vector<std::string> result;
  MapperByName *m = GetMapperMap();
  for (MapperByName::const_iterator it = m->begin(); it != m->end(); ++it) {
    result.push_back(it->second->GetName());
  }
  return result;
}

const PixelMapper *FindPixelMapper(const char *name,
                                   int chain, int parallel,
                                   const char *parameter) {
  std::string lower_name;
  for (const char *n = name; *n; n++) lower_name.append(1, tolower(*n));
  MapperByName::const_iterator found = GetMapperMap()->find(lower_name);
  if (found == GetMapperMap()->end()) {
    fprintf(stderr, "%s: no such mapper\n", name);
    return NULL;
  }
  PixelMapper *mapper = found->second;
  if (mapper == NULL) return NULL;  // should not happen.
  if (!mapper->SetParameters(chain, parallel, parameter))
    return NULL;   // Got parameter, but couldn't deal with it.
  return mapper;
}
}  // namespace rgb_matrix
