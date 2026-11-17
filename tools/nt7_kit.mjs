// NT7 street detail for the modern buildings (rebuilt against reference photographs, E:\nmk\nt7\traits.md):
// fascia lettering, roller shutters with their box and guides, poster vinyl on shop glazing, CCTV, burglar alarm boxes,
// meter cabinets, satellite dishes, bulkhead lamps and plywood boarding. Every piece is k = 0 (no collider, no nav
// change) and sits proud of the wall face it is fixed to (e > 0, no z-fighting). F: a face frame (sideF / tdFace).
import { fbox, faceYaw } from "./parts.mjs";
import { P } from "./kit.mjs";

export function ntMats(A) {
  const add = (n, alb, r, pat, ex = {}) => { if (!A.materials.some((m) => m.name === n)) A.mat(n, alb, r, pat, ex); };
  add("nt_shutter", [0.5, 0.51, 0.51], 0.5, "metal", { texture: "steel_scratched", metallic: 0.45 });
  add("nt_shutter_box", [0.5, 0.51, 0.51], 0.45, "metal", { texture: "steel_scratched", metallic: 0.5 });
  add("nt_letter", [0.92, 0.92, 0.9], 0.5, "plain");
  add("nt_letter_dk", [0.06, 0.06, 0.07], 0.5, "plain");
  add("nt_poster_r", [0.62, 0.08, 0.06], 0.3, "plain");
  add("nt_poster_y", [0.85, 0.66, 0.08], 0.3, "plain");
  add("nt_poster_b", [0.08, 0.2, 0.5], 0.3, "plain");
  add("nt_poster_w", [0.86, 0.86, 0.82], 0.3, "plain");
  add("nt_dev_white", [0.8, 0.8, 0.78], 0.4, "plain");
  add("nt_dev_dark", [0.08, 0.085, 0.09], 0.4, "metal", { metallic: 0.3 });
  add("nt_alarm", [0.85, 0.62, 0.06], 0.4, "plain");
  add("nt_alarm_lens", [0.9, 0.2, 0.05], 0.2, "plain");
  add("nt_meter", [0.55, 0.56, 0.54], 0.6, "plain");
  add("nt_osb", [0.64, 0.5, 0.32], 0.85, "plain");
  add("nt_spray", [0.12, 0.12, 0.13], 0.6, "plain");
}

// 5 x 7 capitals; each row's runs become one bar (fewer parts; tdMerge joins equal runs down the rows)
const FONT = {
  A: [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"], B: ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
  C: [".####", "#....", "#....", "#....", "#....", "#....", ".####"], D: ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
  E: ["#####", "#....", "#....", "####.", "#....", "#....", "#####"], F: ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
  G: [".####", "#....", "#....", "#..##", "#...#", "#...#", ".###."], H: ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
  I: ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"], K: ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
  L: ["#....", "#....", "#....", "#....", "#....", "#....", "#####"], M: ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
  N: ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"], O: [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
  P: ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."], R: ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
  S: [".####", "#....", "#....", ".###.", "....#", "....#", "####."], T: ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
  U: ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."], V: ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
  W: ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"], Y: ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
  "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."], "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
  "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."], "&": [".##..", "#..#.", ".##..", ".#...", "#.#.#", "#..#.", ".##.#"],
  "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."], "'": ["..#..", "..#..", ".....", ".....", ".....", ".....", "....."],
};
// lettering centred at (a, yc) on face F, h tall (7 pixels), e out from the face; returns its width
export function ntText(A, F, a, yc, text, h, e, m = "nt_letter", maxW = Infinity) {
  if (text.length * 6 * (h / 7) - h / 7 > maxW) h = (maxW + h / 7) / (text.length * 6) * 7 * 0.98;   // shrink to fit
  const px = h / 7, adv = 6 * px, w = text.length * adv - px;
  const dir = F.axis === "x" ? F.out : -F.out;   // the viewer's right along the face, seen from outside
  let x0 = a - (dir * w) / 2;
  for (const ch of text) {
    const g = FONT[ch];
    if (g) g.forEach((row, r) => {
      let c = 0;
      while (c < 5) {
        if (row[c] !== "#") { c++; continue; }
        let c1 = c; while (c1 + 1 < 5 && row[c1 + 1] === "#") c1++;
        const ya = yc + h / 2 - (r + 1) * px;
        fbox(A, F, x0 + dir * c * px, x0 + dir * (c1 + 1) * px, ya, ya + px, e, e + 0.012, m, { k: 0, lod: 1 });
        c = c1 + 1;
      }
    });
    x0 += dir * adv;
  }
  return w;
}

// a roller shutter over an opening a0..a1 x y0..y1: the box above, side guides, the curtain down to `frac` of the
// opening (1 = closed), its bottom bar with a lock
export function ntShutter(A, F, a0, a1, y0, y1, frac = 1, { e = 0.02, box = true } = {}) {
  if (box) {   // box: false draws the curtain only (the ruin's, under the clean asset's box)
    fbox(A, F, a0 - 0.08, a1 + 0.08, y1, y1 + 0.32, e, e + 0.3, "nt_shutter_box", { k: 0, bv: 0.01 });
    for (const a of [a0 - 0.05, a1 + 0.05]) fbox(A, F, a - 0.035, a + 0.035, y0, y1, e, e + 0.08, "nt_shutter_box", { k: 0 });
  }
  if (frac <= 0.02) return;
  const yb = y1 - (y1 - y0) * frac;
  fbox(A, F, a0, a1, yb, y1, e + 0.03, e + 0.05, "nt_shutter", { k: 0 });
  for (let y = yb + 0.09; y < y1 - 0.02; y += 0.09) fbox(A, F, a0, a1, y - 0.006, y + 0.006, e + 0.05, e + 0.056, "nt_dev_dark", { k: 0, lod: 1 });   // slat joints
  fbox(A, F, a0, a1, yb, yb + 0.07, e + 0.02, e + 0.07, "nt_shutter_box", { k: 0, bv: 0.008 });
  fbox(A, F, (a0 + a1) / 2 - 0.05, (a0 + a1) / 2 + 0.05, yb + 0.01, yb + 0.06, e + 0.07, e + 0.09, "nt_dev_dark", { k: 0, lod: 1 });
}

// poster vinyl and price cards stuck on shop glazing (just in front of the glass), seeded
export function ntPosters(A, F, a0, a1, y0, y1, seed = 1, e = 0.012) {
  const cols = ["nt_poster_r", "nt_poster_y", "nt_poster_b", "nt_poster_w", "nt_poster_y", "nt_poster_r"];
  let a = a0 + 0.15, k = seed;
  while (a < a1 - 0.5) {
    const w = 0.42 + ((k * 37) % 5) * 0.08, h = 0.55 + ((k * 13) % 4) * 0.1;
    const yb = y0 + 0.1 + ((k * 7) % 3) * 0.25;
    if (yb + h < y1 - 0.05 && a + w < a1 - 0.05) {
      fbox(A, F, a, a + w, yb, yb + h, e, e + 0.004, cols[k % cols.length], { k: 0, lod: 1 });
      fbox(A, F, a + 0.05, a + w - 0.05, yb + h - 0.16, yb + h - 0.08, e + 0.004, e + 0.008, "nt_poster_w", { k: 0, lod: 1 });
    }
    a += w + 0.25 + ((k * 11) % 3) * 0.3;
    k++;
  }
}

export function ntCctv(A, F, a, y, e = 0) {
  fbox(A, F, a - 0.07, a + 0.07, y - 0.1, y + 0.1, e, e + 0.03, "nt_dev_white", { k: 0, lod: 1 });
  fbox(A, F, a - 0.02, a + 0.02, y - 0.02, y + 0.02, e + 0.03, e + 0.2, "nt_dev_white", { k: 0, lod: 1 });
  fbox(A, F, a - 0.06, a + 0.06, y - 0.12, y - 0.0, e + 0.15, e + 0.42, "nt_dev_white", { k: 0, bv: 0.02 });
  fbox(A, F, a - 0.04, a + 0.04, y - 0.1, y - 0.03, e + 0.42, e + 0.43, "nt_dev_dark", { k: 0, lod: 1 });
}
export function ntAlarm(A, F, a, y, e = 0) {
  fbox(A, F, a - 0.16, a + 0.16, y - 0.2, y + 0.2, e, e + 0.08, "nt_alarm", { k: 0, bv: 0.02 });
  fbox(A, F, a - 0.1, a + 0.1, y + 0.08, y + 0.15, e + 0.08, e + 0.1, "nt_alarm_lens", { k: 0, lod: 1 });
}
export function ntMeter(A, F, a, y0, e = 0, w = 0.55, h = 0.7) {
  fbox(A, F, a - w / 2, a + w / 2, y0, y0 + h, e, e + 0.22, "nt_meter", { k: 0, bv: 0.015 });
  fbox(A, F, a - w / 2 + 0.03, a + w / 2 - 0.03, y0 + 0.03, y0 + h - 0.03, e + 0.22, e + 0.225, "nt_meter", { k: 0, lod: 1 });
  fbox(A, F, a + w / 2 - 0.08, a + w / 2 - 0.05, y0 + h / 2 - 0.05, y0 + h / 2 + 0.05, e + 0.225, e + 0.24, "nt_dev_dark", { k: 0, lod: 1 });
}
export function ntDish(A, F, a, y, e = 0) {
  fbox(A, F, a - 0.05, a + 0.05, y - 0.15, y + 0.15, e, e + 0.02, "nt_dev_dark", { k: 0, lod: 1 });
  fbox(A, F, a - 0.015, a + 0.015, y - 0.015, y + 0.015, e + 0.02, e + 0.35, "nt_dev_dark", { k: 0, lod: 1 });
  A.cyl(P(F.axis, a, y + 0.05, F.d + F.out * (e + 0.42)), [0.62, 0.06, 0.62], "nt_dev_white", { k: 0, seg: 16, r: [75, faceYaw(F.axis, F.out), 0] });   // the dish faces out, tilted up
}
export function ntLamp(A, F, a, y, e = 0) {
  fbox(A, F, a - 0.14, a + 0.14, y - 0.09, y + 0.09, e, e + 0.1, "nt_dev_dark", { k: 0, bv: 0.02 });
  fbox(A, F, a - 0.1, a + 0.1, y - 0.06, y + 0.06, e + 0.1, e + 0.11, "nt_dev_white", { k: 0, lod: 1 });
}
// plywood screwed over an opening (a little proud of the frame), a sprayed number on some
export function ntBoard(A, F, a0, a1, y0, y1, { e = 0.03, mark = null } = {}) {
  fbox(A, F, a0 - 0.05, a1 + 0.05, y0 - 0.05, y1 + 0.05, e, e + 0.018, "nt_osb", { k: 0 });
  for (const [a, y] of [[a0, y0], [a1, y0], [a0, y1], [a1, y1], [(a0 + a1) / 2, y0], [(a0 + a1) / 2, y1]])
    fbox(A, F, a - 0.012, a + 0.012, y - 0.012, y + 0.012, e + 0.018, e + 0.022, "nt_dev_dark", { k: 0, lod: 1 });
  if (mark) ntText(A, F, (a0 + a1) / 2, (y0 + y1) / 2, mark, Math.min(0.35, (y1 - y0) * 0.3), e + 0.019, "nt_spray", (a1 - a0) * 0.88);
}

// parts made inside fn belong to the abandoned variant only (Asset.json drops them from the clean asset)
export function ruinOnly(A, fn) { const n = A.parts.length; fn(); for (const p of A.parts.slice(n)) p.ru = 1; }
