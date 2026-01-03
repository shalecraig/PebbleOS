# Graphics and UI System

The graphics system provides efficient rendering for small displays with a hierarchical layer model, dirty-rectangle optimization, and comprehensive drawing primitives.

## Architecture

```
┌─────────────────────────────────────────┐
│  UI Widgets (TextLayer, MenuLayer, ...) │
├─────────────────────────────────────────┤
│  Layer System (composition, clipping)   │
├─────────────────────────────────────────┤
│  Graphics Context (GContext)            │
├─────────────────────────────────────────┤
│  Drawing Primitives (shapes, text, bmp) │
├─────────────────────────────────────────┤
│  Framebuffer (1-bit or 8-bit)           │
├─────────────────────────────────────────┤
│  Display Driver                          │
└─────────────────────────────────────────┘
```

## Graphics Context

**Location**: `src/fw/applib/graphics/graphics.h`

The `GContext` is the central canvas for all drawing:

```c
typedef struct GContext {
  GBitmap dest_bitmap;           // Target framebuffer
  FrameBuffer *parent_framebuffer;
  GDrawState draw_state;         // Colors, clipping, compositing
  TextDrawState text_draw_state;
  FontCache *font_cache;
  bool lock;                     // Prevent operations during capture
} GContext;
```

### Draw State

```c
typedef struct GDrawState {
  GRect clip_box;                // Clipping boundary
  GRect drawing_box;             // Coordinate space origin
  GColor8 stroke_color;
  GColor8 fill_color;
  GColor8 text_color;
  GCompOp compositing_mode;      // Bitmap blending
  uint8_t stroke_width;
  bool antialiased;
} GDrawState;
```

## Layer System

**Location**: `src/fw/applib/ui/layer.h`

Layers form a tree structure with parent-child relationships:

```c
typedef struct Layer {
  GRect frame;                   // Position relative to parent
  GRect bounds;                  // Internal coordinate space
  bool clips;                    // Enable clipping to frame
  bool hidden;
  LayerUpdateProc update_proc;   // Rendering callback
  struct Layer *parent;
  struct Layer *first_child;
  struct Layer *next_sibling;
} Layer;
```

### Layer Rendering

Depth-first traversal with coordinate transformation:

```c
void layer_render_tree(Layer *root, GContext *ctx) {
  for each visible layer:
    adjust ctx->drawing_box by layer->frame
    intersect ctx->clip_box with layer->bounds
    layer->update_proc(layer, ctx)
    layer_render_tree(layer->first_child, ctx)
    restore ctx
}
```

### Dirty Marking

```c
void layer_mark_dirty(Layer *layer);  // Schedule redraw
```

- Automatically marks parent windows dirty
- Deferred rendering prevents redundant draws

## Drawing Primitives

### Shapes

```c
void graphics_draw_pixel(GContext *ctx, GPoint point);
void graphics_draw_line(GContext *ctx, GPoint p0, GPoint p1);
void graphics_draw_rect(GContext *ctx, GRect rect);
void graphics_fill_rect(GContext *ctx, GRect rect, uint16_t corner_radius, GCornerMask corners);
void graphics_draw_circle(GContext *ctx, GPoint center, uint16_t radius);
void graphics_fill_circle(GContext *ctx, GPoint center, uint16_t radius);
void graphics_draw_arc(GContext *ctx, GRect rect, GOvalScaleMode mode,
                        int32_t start_angle, int32_t end_angle);
```

### Paths (Polygons)

```c
GPath *gpath_create(const GPathInfo *info);
void gpath_draw_filled(GContext *ctx, GPath *path);
void gpath_draw_outline(GContext *ctx, GPath *path);
void gpath_rotate_to(GPath *path, int32_t angle);
void gpath_move_to(GPath *path, GPoint offset);
```

### Bitmaps

```c
void graphics_draw_bitmap_in_rect(GContext *ctx, const GBitmap *bitmap, GRect rect);
void graphics_draw_rotated_bitmap(GContext *ctx, const GBitmap *bitmap,
                                   GPoint center, int32_t rotation);
```

### Configuration

```c
void graphics_context_set_stroke_color(GContext *ctx, GColor color);
void graphics_context_set_fill_color(GContext *ctx, GColor color);
void graphics_context_set_stroke_width(GContext *ctx, uint8_t width);
void graphics_context_set_antialiased(GContext *ctx, bool enabled);
void graphics_context_set_compositing_mode(GContext *ctx, GCompOp mode);
```

## Text Rendering

**Location**: `src/fw/applib/graphics/text.h`

```c
void graphics_draw_text(GContext *ctx, const char *text, GFont font,
                        GRect box, GTextOverflowMode overflow,
                        GTextAlignment alignment, GTextAttributes *attrs);

GSize graphics_text_layout_get_content_size(const char *text, GFont font,
                                             GRect box, GTextOverflowMode overflow,
                                             GTextAlignment alignment);
```

### Features

- **UTF-8**: Full Unicode support
- **Overflow modes**: Word wrap, trailing ellipsis, fill
- **Alignment**: Left, center, right
- **Layout caching**: Optional for performance

### Fonts

```c
GFont fonts_get_system_font(const char *font_key);
GFont fonts_load_custom_font(ResHandle handle);
```

## Color System

**GColor8**: 8-bit ARGB format (2 bits per channel)

```c
typedef union {
  uint8_t argb;
  struct {
    uint8_t b:2;  // Blue 0-3
    uint8_t g:2;  // Green 0-3
    uint8_t r:2;  // Red 0-3
    uint8_t a:2;  // Alpha 0-3
  };
} GColor8;
```

### Color Utilities

```c
GColor GColorFromRGB(uint8_t r, uint8_t g, uint8_t b);
GColor GColorFromHEX(uint32_t hex);
bool gcolor_equal(GColor a, GColor b);
GColor gcolor_legible_over(GColor background);  // Contrast color
```

### Predefined Colors

64 named colors: `GColorBlack`, `GColorWhite`, `GColorRed`, `GColorCyan`, etc.

## Compositing Modes

| Mode | B&W Effect | Color Effect |
|------|------------|--------------|
| `GCompOpAssign` | `dest = src` | Direct replacement |
| `GCompOpAssignInverted` | `dest = ~src` | Inverted source |
| `GCompOpOr` | `dest |= src` | White paints through |
| `GCompOpAnd` | `dest &= src` | Black paints through |
| `GCompOpSet` | Transparency | Alpha blending |
| `GCompOpTint` | N/A | Tint with color |

## Animation System

**Location**: `src/fw/applib/ui/animation.h`

```c
Animation *animation_create(void);
void animation_set_duration(Animation *anim, uint32_t duration_ms);
void animation_set_curve(Animation *anim, AnimationCurve curve);
void animation_set_handlers(Animation *anim, AnimationHandlers handlers, void *context);
void animation_schedule(Animation *anim);
```

### Animation Curves

- `AnimationCurveLinear` - Constant velocity
- `AnimationCurveEaseIn` - Accelerate from zero
- `AnimationCurveEaseOut` - Decelerate to zero
- `AnimationCurveEaseInOut` - Accelerate then decelerate
- `AnimationCurveCustomFunction` - Custom curve function

### Property Animation

```c
PropertyAnimation *property_animation_create_layer_frame(
    Layer *layer, GRect *from, GRect *to);
```

### Composite Animations

```c
Animation *animation_sequence_create(Animation *a, Animation *b, ...);
Animation *animation_spawn_create(Animation *a, Animation *b, ...);
```

## UI Widgets

### BitmapLayer

```c
BitmapLayer *bitmap_layer_create(GRect frame);
void bitmap_layer_set_bitmap(BitmapLayer *layer, const GBitmap *bitmap);
void bitmap_layer_set_alignment(BitmapLayer *layer, GAlign alignment);
void bitmap_layer_set_compositing_mode(BitmapLayer *layer, GCompOp mode);
```

### TextLayer

```c
TextLayer *text_layer_create(GRect frame);
void text_layer_set_text(TextLayer *layer, const char *text);
void text_layer_set_font(TextLayer *layer, GFont font);
void text_layer_set_text_alignment(TextLayer *layer, GTextAlignment alignment);
void text_layer_set_overflow_mode(TextLayer *layer, GTextOverflowMode mode);
```

### MenuLayer

```c
MenuLayer *menu_layer_create(GRect frame);
void menu_layer_set_callbacks(MenuLayer *layer, void *context, MenuLayerCallbacks callbacks);
```

### ScrollLayer

```c
ScrollLayer *scroll_layer_create(GRect frame);
void scroll_layer_set_content_size(ScrollLayer *layer, GSize size);
void scroll_layer_add_child(ScrollLayer *layer, Layer *child);
```

## Framebuffer

**Location**: `src/fw/applib/graphics/framebuffer.h`

```c
void framebuffer_mark_dirty_rect(FrameBuffer *fb, GRect rect);
bool framebuffer_is_dirty(FrameBuffer *fb);
void framebuffer_reset_dirty(FrameBuffer *fb);
```

### Formats

- **1-bit**: Black and white (Aplite)
- **8-bit**: 64-color palette (Basalt, Chalk)
- **8-bit circular**: Variable row widths for round displays

## Performance Optimizations

1. **Dirty rectangles**: Only update changed regions
2. **Layout caching**: Cache text layout calculations
3. **Word-aligned bitblt**: 32-bit operations for 1-bit displays
4. **Lookup tables**: Pre-computed color blending, luminance
5. **Inline functions**: Fixed-point arithmetic optimized
