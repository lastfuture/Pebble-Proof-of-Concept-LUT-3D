#include <pebble.h>

#define ANTIALIASING true
#define WHWIDTH 18
#define DITHERFACTOR 85
#define LINES_PER_CHUNK 100
#define FRAME_DURATION 35

static uint8_t bayer8x8[] = {
   0,32, 8,40, 2,34,10,42,
  48,16,56,24,50,18,58,26,
  12,44, 4,36,14,46, 6,38,
  60,28,52,20,62,30,54,22,
   3,35,11,43, 1,33, 9,41,
  51,19,59,27,49,17,57,25,
  15,47, 7,39,13,45, 5,37,
  63,31,55,23,61,29,53,21,
};

typedef union GColor32 {
  uint32_t argb;
  struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
  };
} GColor32;

typedef struct {
  GContext *ctx;
	Window* window;
	Layer* layer;
  AppTimer *timer;
  uint32_t timer_timeout;
  uint16_t animation_angle;
  GPoint center;
} State;

// global state
static State g;

static bool debug = true;

/************************************ UI **************************************/

static void render_part(State *state, Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect texturebounds = GRect(state->center.x-64, state->center.y-64, 127, 127);
  GRect bounds_mo = grect_inset(texturebounds, GEdgeInsets(9));
  GRect bounds_ho = grect_inset(texturebounds, GEdgeInsets(23));
  
  uint16_t minute_deg = state->animation_angle;
  uint16_t hour_deg = 360-minute_deg;
  GPoint minute_hand_outer = gpoint_from_polar(bounds_mo, GOvalScaleModeFillCircle, DEG_TO_TRIGANGLE(minute_deg));
  GPoint hour_hand_outer = gpoint_from_polar(bounds_ho, GOvalScaleModeFillCircle, DEG_TO_TRIGANGLE(hour_deg));
  
  // draw the watch hands that will be used as texture
  graphics_context_set_antialiased(ctx, ANTIALIASING);
  graphics_context_set_fill_color(ctx, GColorShockingPink);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
  graphics_context_set_stroke_color(ctx, GColorCobaltBlue);
  graphics_context_set_stroke_width(ctx, WHWIDTH);
  graphics_draw_line(ctx, state->center, minute_hand_outer);
  
  graphics_context_set_stroke_color(ctx, GColorCeleste);
  graphics_context_set_stroke_width(ctx, WHWIDTH);
  graphics_draw_line(ctx, state->center, hour_hand_outer);
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, state->center, WHWIDTH/4);
  
  // START OF TEXTURE MAPPING
  GColor bgcolor = GColorIcterine; // background color for behind the objects
  
  // load map parts
  ResHandle lut_handle = resource_get_handle(RESOURCE_ID_MAP_TEST);
  size_t lut_res_size = 144*LINES_PER_CHUNK*2;
  uint8_t *lut_buffer = (uint8_t*)malloc(lut_res_size);
  resource_load_byte_range(lut_handle, 0, lut_buffer, lut_res_size);
  
  ResHandle shad_handle = resource_get_handle(RESOURCE_ID_SHADING_TEST);
  size_t shad_res_size = 144*LINES_PER_CHUNK;
  uint8_t *shad_buffer = (uint8_t*)malloc(shad_res_size);
  resource_load_byte_range(shad_handle, 0, shad_buffer, shad_res_size);
  
  // define mapping metadata
  GSize mapdimensions = GSize(144, LINES_PER_CHUNK);
  //GPoint master_offset = GPoint(state->center.x-(mapdimensions.w/2), state->center.y-(mapdimensions.h/2)+4);
  GPoint master_offset = GPoint(0, 0);
  GRect mapbounds = GRect(master_offset.x, master_offset.y, mapdimensions.w, mapdimensions.h);
  
  // capture frame buffer
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  
  // set up texture buffer
  GSize texturesize = GSize(texturebounds.size.w, texturebounds.size.h);
  GBitmap *texture = gbitmap_create_blank(texturesize, GBitmapFormat8Bit);
  uint8_t (*texture_matrix)[texturesize.w] = (uint8_t (*)[texturesize.w]) gbitmap_get_data(texture);
  
  // capture texture before starting to modify the frame buffer
  for(uint8_t y = 0; y < bounds.size.h; y++) {
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    for(uint8_t x = info.min_x; x <= info.max_x; x++) {
      if (x >= texturebounds.origin.x && y >= texturebounds.origin.y && x < texturebounds.origin.x+texturebounds.size.w && y < texturebounds.origin.y+texturebounds.size.h) {
        texture_matrix[y-texturebounds.origin.y][x-texturebounds.origin.x] = info.data[x];
      }
    }
  }
  
  // render texture mapped and shaded object
  for(uint8_t y = 0; y < bounds.size.h; y++) {
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    for(uint8_t x = info.min_x; x <= info.max_x; x++) {
      GColor fbpixel = bgcolor;
      
      // convert to 24 bit color
      GColor32 newpixel;
      newpixel.r = fbpixel.r*DITHERFACTOR;
      newpixel.g = fbpixel.g*DITHERFACTOR;
      newpixel.b = fbpixel.b*DITHERFACTOR;
      newpixel.a = 0xff;
      
      // render texture mapped object by looking up pixels in the lookup table
      if (x >= mapbounds.origin.x && y >= mapbounds.origin.y && x < mapbounds.origin.x+mapbounds.size.w && y < mapbounds.origin.y+mapbounds.size.h) {
        uint16_t surfindex = (x-mapbounds.origin.x)+((y-mapbounds.origin.y)*mapbounds.size.w);
        
        uint8_t xpos = lut_buffer[surfindex*2];
        uint8_t ypos = lut_buffer[(surfindex*2)+1];
        if (xpos > 0 || ypos > 0) {
          uint8_t texturexpos = xpos/2;
          uint8_t textureypos = 127-(ypos/2);
          GColor texturepixel = (GColor8) texture_matrix[textureypos][texturexpos];
          newpixel.r = texturepixel.r*DITHERFACTOR;
          newpixel.g = texturepixel.g*DITHERFACTOR;
          newpixel.b = texturepixel.b*DITHERFACTOR;
          if (xpos%2 == 1 || ypos%2 == 1) {
            // interpolate
            GColor texturepixel2;
            if (texturexpos < 255 && xpos%2 == 1) {
              texturepixel2.argb = texture_matrix[textureypos][texturexpos+1];
              newpixel.r = (newpixel.r + texturepixel2.r*DITHERFACTOR)/2;
              newpixel.g = (newpixel.g + texturepixel2.g*DITHERFACTOR)/2;
              newpixel.b = (newpixel.b + texturepixel2.b*DITHERFACTOR)/2;
            }
            if (textureypos > 1 && ypos%2 == 1) {
              texturepixel2.argb = texture_matrix[textureypos-1][texturexpos];
              newpixel.r = (newpixel.r + texturepixel2.r*DITHERFACTOR)/2;
              newpixel.g = (newpixel.g + texturepixel2.g*DITHERFACTOR)/2;
              newpixel.b = (newpixel.b + texturepixel2.b*DITHERFACTOR)/2;
            }
            newpixel.r = (newpixel.r/DITHERFACTOR)*DITHERFACTOR;
            newpixel.g = (newpixel.g/DITHERFACTOR)*DITHERFACTOR;
            newpixel.b = (newpixel.b/DITHERFACTOR)*DITHERFACTOR;
          }
        }
        
        uint8_t specularmap = shad_buffer[surfindex];
        uint8_t shadowmap = (specularmap & 0b00001111)*16;
        specularmap = ((specularmap & 0b11110000) >> 4)*16;
        
        // subtract shadows
        newpixel.r -= (newpixel.r > shadowmap) ? shadowmap : newpixel.r;
        newpixel.g -= (newpixel.g > shadowmap) ? shadowmap : newpixel.g;
        newpixel.b -= (newpixel.b > shadowmap) ? shadowmap : newpixel.b;
        
        // add highlights
        newpixel.r += (255-newpixel.r > specularmap) ? specularmap : 255-newpixel.r;
        newpixel.g += (255-newpixel.g > specularmap) ? specularmap : 255-newpixel.g;
        newpixel.b += (255-newpixel.b > specularmap) ? specularmap : 255-newpixel.b;
      }
      
      // here comes the actual dithering
      uint8_t bayerpixel = bayer8x8[((x%8)+(y*8))%64];
      fbpixel.r = (newpixel.r+bayerpixel)/DITHERFACTOR;
      fbpixel.g = (newpixel.g+bayerpixel)/DITHERFACTOR;
      fbpixel.b = (newpixel.b+bayerpixel)/DITHERFACTOR;
      fbpixel.a = 0b11;
      
      memset(&info.data[x], fbpixel.argb, 1);
    }
  }
  
  // free memory
  free(lut_buffer);
  free(shad_buffer);
  gbitmap_destroy(texture);
  
  // release frame buffer
  graphics_release_frame_buffer(ctx, fb);
}

/*********************************** App **************************************/

void animation_init(State* state) {
  state->animation_angle = 0;
  GRect window_bounds = layer_get_bounds(state->layer);
  state->center = grect_center_point(&window_bounds);
  state->center.x -= 1;
  state->center.y -= 1;
}
static void animation_layer_update(Layer *layer, GContext *ctx) {
  render_part(&g, layer, ctx);
}

void animation_update(void* data) {
  State* state = (State*)data;
	state->animation_angle += (state->animation_angle < 360) ? 1 : 0-state->animation_angle;
  layer_mark_dirty(state->layer);
  state->timer = app_timer_register(state->timer_timeout, &animation_update, data);
}

static void init() {
  g.window = window_create();
  window_stack_push(g.window, true);
  Layer* window_layer = window_get_root_layer(g.window);
  GRect window_frame = layer_get_frame(window_layer);
  
  g.layer = layer_create(window_frame);
  layer_set_update_proc(g.layer, &animation_layer_update);
  layer_add_child(window_layer, g.layer);
  
  animation_init(&g);
  
  g.timer_timeout = FRAME_DURATION;
  g.timer = app_timer_register(g.timer_timeout, &animation_update, &g);
  
  if (debug) {
    light_enable(true);
  }
}

static void deinit() {
  app_timer_cancel(g.timer);
  window_destroy(g.window);
  layer_destroy(g.layer);
}

int main() {
  init();
  app_event_loop();
  deinit();
}




