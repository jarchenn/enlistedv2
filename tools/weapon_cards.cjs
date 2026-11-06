// Generates docs/weapons.md (weapon reference cards) from the shipped data, so the cards can never
// disagree with what the game loads.  Usage: node tools/weapon_cards.cjs
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..');
const W = id => JSON.parse(fs.readFileSync(path.join(root, 'content/weapons', id + '.json'), 'utf8'));
const A = id => JSON.parse(fs.readFileSync(path.join(root, 'content/assets', id + '.json'), 'utf8'));

const SOURCES = {
  1: 'U.S. War Department, *Handbook on German Military Forces*, TM-E 30-451 (March 1945), ch. VII, 7.5 cm Pak 40 characteristics. https://lonesentry.com/manuals/tme30/ch7sec4sub6.html',
  2: 'TM-E 30-451, ch. VIII, fire-control equipment for the 7.5 cm Pak 40; preserved-gun photographs for the optic assembly. https://t.lonesentry.com/manuals/tme30/ch8sec3.html ; https://www.lovettartillery.com/7%2C5cm_Panzer_Abwehr_Kanone_PAK_1940.html',
  3: 'Battle Order, *U.S. Army & Marine Ammo Loads During WWII* (secondary reconstruction). https://www.battleorder.org/post/us-ammo-load-ww2',
  4: 'American Rifleman, *The G.I. Thompson in World War II* (carrier photographs: five-pocket 20-round, three-pocket 30-round). https://www.americanrifleman.org/articles/2019/2/20/the-gi-thompson-in-world-war-ii/',
  5: 'German rifle ammunition pouches, photographed with compartment and clip capacities. https://www.mp44.nl/equipment/ammo_pouches.htm',
  6: 'U.S. War Department, *Intelligence Bulletin*, April 1943, German infantry division firepower (formation-level estimate, not a per-soldier beach load). https://dokumen.pub/military-intelligence-service-war-department-1943-04-intelligence-bulletin-vol-01-no08.html',
  7: 'American Rifleman, *The M1C Garand Sniper Rifle*; *Garand\'s military rifles*. Adoption date accounts differ: shipped as a later-war sandbox option. https://www.americanrifleman.org/articles/2014/8/29/the-m1c-garand-sniper-rifle/ ; https://www.americanrifleman.org/content/garand-s-military-rifles-m1-to-m14/',
  8: 'American Rifleman, *World War II German Sniper Systems* (ZF39 low-turret vs. other mounts). https://www.americanrifleman.org/content/world-war-ii-german-sniper-systems-rifles-optics-ammo/',
  9: 'British War Office, *Pocket Book of the German Army* (1943), infantry weapons table, joinable 50-round belts. https://www.ibiblio.org/hyperwar/NHC/NewPDFs/UK/UK%20Pocket%20Book%20on%20German%20Army%201943.pdf',
  10: 'U.S. Army, *Standard Ordnance Items Catalog* (1944) vol. 3, Browning M2 HB, flexible ground mount, 110-round increments. https://www.90thidpg.us/Reference/Manuals/Standard%20Ordnance%20Items%20Catalog%2C%201944%2C%20Vol%203.pdf',
};
const RELOAD = { enbloc: 'En-bloc clip. Empty: clip ejects with a ping, new clip. Partial: the remaining clip is kept as a reserve container (rounds are never lost), a fresh clip is loaded.',
  stripper: 'Stripper clips through the open bolt: a full clip when there is room for 5, otherwise single rounds top off the magazine.',
  single: 'Single rounds only (the scope blocks stripper-clip loading), one round per reload step.',
  magazine: 'Detachable magazine swap. The removed magazine keeps its own count and goes back into the reserve.',
  belt: 'Belt change. A partly used belt segment returns to the reserve with its count.' };

function weaponCard(id) {
  const w = W(id);
  const total = w.loaded + w.reserve;
  const lines = [];
  lines.push(`### ${w.name} (\`${w.id}\`)${w.laterWar ? '  *later-war option (hidden by the strict 6 June 1944 preset)*' : ''}`);
  lines.push('');
  lines.push('| | |');
  lines.push('|---|---|');
  lines.push(`| Side / calibre | ${w.side} / ${w.caliber} |`);
  lines.push(`| Action / feed | ${w.action} / ${w.feed} (${w.container}, ${w.containerSize} per container) |`);
  lines.push(`| Capacity | ${w.cap} in the weapon |`);
  lines.push(`| Shipped preset | ${w.loaded} loaded + ${w.reserve} reserve = **${total}** (the allowance a crate refills to) |`);
  lines.push(`| Cyclic / practical rate (game) | ${w.rpm} rpm${w.boltTime ? `, bolt cycle ${w.boltTime} s` : ''} |`);
  lines.push(`| Muzzle velocity (game) | ${w.velocity} m/s, declared zero ${w.zero} m |`);
  if (w.scope) lines.push(`| Optic | ${w.scope.mag}x (view FOV = 72/${w.scope.mag} deg)${w.scope.irons ? ', iron sights available via V' : ''} |`);
  lines.push(`| Reload (game seconds) | empty ${w.reload?.empty ?? '-'}${w.reload?.partial ? `, partial ${w.reload.partial}` : ''}${w.reload?.round ? `, per round ${w.reload.round}` : ''} |`);
  lines.push(`| Recoil (game units) | pitch ${w.recoil?.pitch}, yaw ${w.recoil?.yaw}, recovery ${w.recoil?.recover}/s${w.recoil?.climb ? `, climb ${w.recoil.climb}` : ''}${w.recoil?.supported ? `, bipod-supported x${w.recoil.supported}` : ''} |`);
  lines.push('');
  lines.push(`**Reload model.** ${RELOAD[w.feed] || w.feed}`);
  lines.push('');
  lines.push(`**Note.** ${w.note || ''}`);
  lines.push('');
  return lines.join('\n');
}
function stationCard(id, title, src) {
  const a = A(id), s = a.station;
  return [`### ${title} (\`${id}\`)`, '', '| | |', '|---|---|',
    `| Traverse / elevation | ${s.traverse[0]} to +${s.traverse[1]} deg / ${s.elevation[0]} to +${s.elevation[1]} deg |`,
    `| Hand-wheel rates (game) | ${s.traverseRate} deg/s traverse, ${s.elevateRate} deg/s elevation |`,
    `| Ammunition | \`${s.ammo}\`: ${s.loaded} loaded + ${s.reserve} reserve; reload ${s.reloadTime} s |`,
    `| Crate service radius | ${s.serviceRadius} m |`,
    `| Positions | gunner (Aim) and handler (Move); T switches, E leaves |`,
    '', `**Note.** ${(s.note || '')} ${a.description || ''}`.trim(), '', `Sources: ${src}`, ''].join('\n');
}

const weapons = ['m1_garand', 'm1c_garand', 'm1_thompson', 'kar98k', 'kar98k_zf39', 'mp40', 'mg42'];
const out = [];
out.push('# Weapon reference cards');
out.push('');
out.push('Generated by `node tools/weapon_cards.cjs` from `content/weapons/*.json` and the station blocks of `content/assets/gun_*.json`. Edit the data, not this file.');
out.push('');
out.push('**Game numbers are tuning, not measurements.** Rates, velocities, recoil and reload times are values chosen so the sandbox feels plausible; they are not claims about a particular weapon on a particular day. Where a figure comes from the brief\'s sources it is cited inline in the weapon note (bracketed numbers below).');
out.push('');
out.push('**No reference images were downloaded or shipped.** The brief lists photographs (carriers, pouches, the preserved PaK 40 optic). They were used only as descriptions to check proportions and container capacities; nothing from those pages is redistributed. All geometry is generated from boxes and cylinders by `tools/catalog_*.mjs`.');
out.push('');
out.push('## Personal weapons (eight entries with the M2HB station below)');
out.push('');
for (const id of weapons) out.push(weaponCard(id));
out.push('## Crew-served stations');
out.push('');
out.push(stationCard('gun_pak40', '7.5 cm PaK 40', '[1] characteristics (65 deg traverse, -5 to +22 deg elevation), [2] optic placement.'));
out.push(stationCard('gun_m2hb_tripod', 'Browning M2 HB on M3 tripod', '[10] flexible ground mount, 110-round increments (crate unit is one 110-round belt).'));
out.push('## Ammunition accounting');
out.push('');
out.push('- Every round lives in exactly one place: the weapon, a reserve container (clip, magazine, belt segment, each with its own count), a station, or a crate. Reloads move containers; nothing is created or destroyed except by firing.');
out.push('- A crate refills whole containers up to the weapon\'s allowance (loaded + reserve of the shipped preset). It never tops up a partial container and never exceeds the allowance.');
out.push('- Finite crates count down; an empty crate refuses. Returning to Creative (F5) discards the session: crates, stations and personal loads go back to their authored values.');
out.push('');
out.push('## Sources');
out.push('');
for (const [k, v] of Object.entries(SOURCES)) out.push(`[${k}] ${v}`), out.push('');
fs.writeFileSync(path.join(root, 'docs/weapons.md'), out.join('\n'));
console.log('wrote docs/weapons.md');
