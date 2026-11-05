// Summarise destructible panels per asset: node tools/panel_report.cjs
const fs = require("fs");
const dir = "content/assets";
for (const f of fs.readdirSync(dir).sort()) {
  const j = JSON.parse(fs.readFileSync(`${dir}/${f}`, "utf8"));
  const panels = j.panels || [];
  const perFloor = {};
  for (const p of panels) { const k = p.id.split("/")[0]; perFloor[k] = (perFloor[k] || 0) + 1; }
  const fam = [...new Set(panels.map((p) => p.family))].join(",");
  const widths = panels.map((p) => Math.max(p.max[0] - p.min[0], p.max[2] - p.min[2]) - Math.min(p.max[0] - p.min[0], p.max[2] - p.min[2]));
  console.log(`${j.id.padEnd(28)} ${String(panels.length).padStart(3)} panels  ${fam.padEnd(9)} ${JSON.stringify(perFloor)}  bay width ${Math.min(...widths).toFixed(2)}-${Math.max(...widths).toFixed(2)} m`);
}
