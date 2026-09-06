// FlugVel enclosure - v2 DRAFT, "premium" pass
// One-piece case for the SZHJW "2.4 TFT + EC11" display/encoder/button board
// with an ESP32-D DevKit (38-pin DOIT-style clone) stacked directly behind it,
// wired together internally through a slot instead of routed outside the case.
//
// Styling pass on top of the v1 functional draft: soft-rounded corners
// (like an injection-molded shell, not a raw box), a chamfered lip around
// the display glass so the bezel doesn't look like a laser-cut plate, a
// recessed rabbet seam where the lid meets the shell (a thin, tight
// reveal line instead of a visible butt joint - this is most of what
// makes a print look "expensive" vs. "hobby project"), and an engraved
// logo on the back.
//
// All dimensions are either measured from the Printables STLs (display
// board) or standard published figures for the DOIT ESP32 DevKit v1 clone -
// NOT verified against your physical parts with calipers. Print
// PRINT_MODE="test" first (just the front panel) and check the fit before
// committing to a full print - see the comment block below it.
//
// Render/export: install OpenSCAD (openscad.org, free), open this file.
// F5 = preview, F6 = full render, then File > Export > Export as STL.
// $fn is kept modest (32-64) for preview speed - bump RENDER_FN below for
// the final export if you want smoother curves.

/* ---------------- PRINT MODE ----------------
   "test"     - just the front panel, for a fast fit-check print
   "shell"    - the full case shell (front + ESP32 back compartment), no lid
   "lid"      - just the back lid
   "assembly" - shell + lid laid out side by side, for previewing together
*/
PRINT_MODE = "test";
RENDER_FN = 48;

/* ---------------- DISPLAY BOARD (measured from STL) ---------------- */
BOARD_L = 97.0;
BOARD_W = 50.0;
BOARD_T = 1.8;

FRONT_COMP_H = 11.0;   // encoder body + button housing height above the PCB (not the knob)
BACK_COMP_H  = 9.0;    // header pins / solder side height behind the PCB

// Display glass cutout - ADJUST after test-fitting, this is the number
// most worth double-checking against your real board.
DISP_CUT_L = 50.0;
DISP_CUT_W = 40.0;
DISP_CUT_X = 4.0;
DISP_CUT_Y = (BOARD_W - DISP_CUT_W) / 2;
DISP_BEVEL = 1.2;      // chamfer depth around the window, outward-facing side

// Encoder shaft + push-button holes, front panel.
ENC_HOLE_D = 7.2;
ENC_HOLE_X = 74.0;
ENC_HOLE_Y = 36.0;
BTN_HOLE_D = 6.2;
BTN_HOLE_X = 88.0;
BTN_HOLE_Y = 32.0;
HOLE_COUNTERSINK = 0.6; // shallow recess around each hole so knob/cap sit flush, not proud

BOARD_HOLE_INSET = 4.0;
BOARD_HOLE_D     = 2.4;
BOARD_BOSS_D     = 6.0;

/* ---------------- ESP32 DEVKIT (standard DOIT v1 clone figures) ---------------- */
ESP_L = 50.0;
ESP_W = 27.0;
ESP_COMP_H = 8.0;
ESP_HOLE_INSET = 3.0;
ESP_HOLE_D = 2.2;
ESP_BOSS_D = 5.0;

USB_CUT_W = 10.5;
USB_CUT_H = 5.0;

/* ---------------- SHARED / STYLING ---------------- */
WALL = 2.0;
WIRE_GAP = 6.0;
CORNER_R = 4.0;         // outer-perimeter corner radius - the main "not a box" cue
SEAM_LIP = 1.0;          // depth of the recessed rabbet the lid sits into
SEAM_GAP = 0.15;         // per-side clearance in the rabbet so the lid seats without forcing
LOGO_TEXT = "FLUGVEL";
LOGO_SIZE = 6;
LOGO_DEPTH = 0.4;        // engraved, not embossed - cleaner under paint/light

/* ==================== derived ==================== */
CASE_L = BOARD_L + 2 * WALL;
CASE_W = BOARD_W + 2 * WALL;
FRONT_DEPTH = WALL + FRONT_COMP_H;
MID_DEPTH   = BOARD_T + BACK_COMP_H + WIRE_GAP;
ESP_DEPTH   = ESP_COMP_H + 1.5;
SHELL_D     = FRONT_DEPTH + MID_DEPTH + ESP_DEPTH; // shell depth, open back face
CASE_D      = SHELL_D + WALL;                       // + lid

/* ---------------- primitives ---------------- */

// 2D rounded rectangle, corners only (flat top/bottom when extruded - this
// is what makes a case look "molded" without turning every edge into a
// ball-nosed blob).
module rrect(l, w, r) {
    hull() {
        for (x = [r, l - r])
            for (y = [r, w - r])
                translate([x, y]) circle(r = r, $fn = RENDER_FN);
    }
}

module rrect_prism(l, w, h, r) {
    linear_extrude(height = h) rrect(l, w, r);
}

module corner_bosses(l, w, inset, hole_d, boss_d, h) {
    for (x = [inset, l - inset])
        for (y = [inset, w - inset])
            translate([x, y, 0])
                difference() {
                    cylinder(d = boss_d, h = h, $fn = 32);
                    translate([0, 0, -0.5]) cylinder(d = hole_d, h = h + 1, $fn = 24);
                }
}

// Chamfered cutter: a frustum that's DISP_BEVEL wider at z=0 (outward face)
// than at z=depth, so the window edge reads as a machined bevel instead of
// a raw vertical wall.
module beveled_window(l, w, r, depth, bevel) {
    hull() {
        translate([0, 0, 0])          rrect_prism(l + 2 * bevel, w + 2 * bevel, 0.01, r + bevel);
        translate([bevel, bevel, bevel]) rrect_prism(l, w, depth, r);
    }
}

module countersunk_hole(d, h, sink) {
    union() {
        cylinder(d = d, h = h, $fn = RENDER_FN);
        translate([0, 0, -0.01])
            cylinder(d1 = d + 2 * sink, d2 = d, h = sink + 0.01, $fn = RENDER_FN);
    }
}

/* ---------------- body ---------------- */

module front_panel() {
    difference() {
        rrect_prism(CASE_L, CASE_W, WALL, CORNER_R);

        translate([WALL + DISP_CUT_X, WALL + DISP_CUT_Y, -0.01])
            beveled_window(DISP_CUT_L, DISP_CUT_W, 1.5, WALL + 0.02, DISP_BEVEL);

        translate([WALL + ENC_HOLE_X, WALL + ENC_HOLE_Y, -0.5])
            countersunk_hole(ENC_HOLE_D, WALL + 1, HOLE_COUNTERSINK);
        translate([WALL + BTN_HOLE_X, WALL + BTN_HOLE_Y, -0.5])
            countersunk_hole(BTN_HOLE_D, WALL + 1, HOLE_COUNTERSINK);
    }
}

module display_side_walls() {
    difference() {
        rrect_prism(CASE_L, CASE_W, FRONT_DEPTH + MID_DEPTH, CORNER_R);
        translate([WALL, WALL, -1])
            cube([BOARD_L, BOARD_W, FRONT_DEPTH + MID_DEPTH + 2]);
    }
}

module esp_compartment() {
    esp_l = ESP_L + 2 * WALL;
    esp_w = ESP_W + 2 * WALL;
    x0 = (CASE_L - esp_l) / 2;
    y0 = (CASE_W - esp_w) / 2;

    difference() {
        translate([x0, y0, 0]) rrect_prism(esp_l, esp_w, ESP_DEPTH + WALL, CORNER_R * 0.6);
        translate([x0 + WALL, y0 + WALL, WALL])
            cube([ESP_L, ESP_W, ESP_DEPTH + 1]);

        translate([x0 + esp_l / 2 - USB_CUT_W / 2, y0 - 1, WALL + (ESP_DEPTH - USB_CUT_H) / 2])
            cube([USB_CUT_W, WALL + 2, USB_CUT_H]);
    }

    translate([x0 + WALL, y0 + WALL, WALL])
        corner_bosses(ESP_L, ESP_W, ESP_HOLE_INSET, ESP_HOLE_D, ESP_BOSS_D, 3);
}

module case_shell() {
    difference() {
        union() {
            front_panel();
            translate([0, 0, WALL]) display_side_walls();
            translate([0, 0, WALL]) corner_bosses(CASE_L, CASE_W, BOARD_HOLE_INSET + WALL,
                                                    BOARD_HOLE_D, BOARD_BOSS_D, FRONT_COMP_H - 2);
            translate([0, 0, FRONT_DEPTH + MID_DEPTH]) esp_compartment();
        }
        // recessed rabbet the lid seats into, cut into the open back face -
        // this is the tight-reveal seam instead of a butt joint.
        translate([WALL + SEAM_GAP, WALL + SEAM_GAP, SHELL_D - SEAM_LIP])
            rrect_prism(CASE_L - 2 * (WALL + SEAM_GAP), CASE_W - 2 * (WALL + SEAM_GAP),
                        SEAM_LIP + 0.5, max(CORNER_R - WALL, 0.5));
    }
}

module lid() {
    difference() {
        rrect_prism(CASE_L, CASE_W, WALL, CORNER_R);
        // engraved logo, back face
        translate([CASE_L / 2, CASE_W / 2, WALL - LOGO_DEPTH])
            linear_extrude(height = LOGO_DEPTH + 0.01)
                text(LOGO_TEXT, size = LOGO_SIZE, halign = "center", valign = "center",
                     font = "Liberation Sans:style=Bold");
    }
    // the tab that presses into case_shell()'s rabbet
    translate([WALL + SEAM_GAP + 0.05, WALL + SEAM_GAP + 0.05, -SEAM_LIP])
        rrect_prism(CASE_L - 2 * (WALL + SEAM_GAP) - 0.1, CASE_W - 2 * (WALL + SEAM_GAP) - 0.1,
                    SEAM_LIP, max(CORNER_R - WALL, 0.5));
}

/* ==================== output ==================== */
if (PRINT_MODE == "test") {
    front_panel();
} else if (PRINT_MODE == "shell") {
    case_shell();
} else if (PRINT_MODE == "lid") {
    lid();
} else if (PRINT_MODE == "assembly") {
    case_shell();
    translate([CASE_L + 15, 0, 0]) lid();
}
