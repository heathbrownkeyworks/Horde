// npm --prefix tools/imgui-preview ci
// npm --prefix tools/imgui-preview run icons
// Generated font contains the artwork in assets/icons/.
import fs from 'node:fs';
import {Readable} from 'node:stream';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {SVGIcons2SVGFontStream}=require('svgicons2svgfont');
const svg2ttf=require('svg2ttf');
// Array order preserves the private Unicode code points used by the screen.
// Attribution for all 19 Game-icons.net glyphs is recorded in licenses/.
const names=JSON.parse(fs.readFileSync('assets/icons/glyphs.json','utf8'));
if(names.length!==19 || new Set(names).size!==19 || names.some(name=>!/^\w+$/.test(name)))
    throw new Error('Expected 19 unique source icon names');
const stream=new SVGIcons2SVGFontStream({fontName:'HordeIcons',fontHeight:1024,normalize:true,log:()=>{}});
let xml='';stream.on('data',chunk=>xml+=chunk);
const done=new Promise((resolve,reject)=>{stream.on('finish',resolve);stream.on('error',reject);});
let header='#pragma once\n// Generated from assets/icons/ by tools/imgui-preview/build-icons.mjs.\nnamespace Horde::ImGuiUI::Icons\n{\n';
for(let i=0;i<names.length;++i) {
    const name=names[i];
    const svg=fs.readFileSync(`assets/icons/${name}.svg`,'utf8').trim();
    const glyph=Readable.from([svg]);glyph.metadata={unicode:[String.fromCodePoint(0xe000+i)],name};stream.write(glyph);
    header+=`    inline constexpr const char* ${name}="${String.fromCodePoint(0xe000+i)}";\n`;
}
stream.end();await done;
fs.writeFileSync('assets/fonts/HordeIcons.ttf',Buffer.from(svg2ttf(xml,{
    ts:1789776000,
    copyright:'Icons by the credited Game-icons.net authors. CC BY 3.0. See licenses/Game-Icons-Attribution.md.',
    url:'https://game-icons.net',
    description:'Original Horde SVG paths converted to a monochrome TrueType icon font.'
}).buffer));
fs.writeFileSync('src/ui/imgui/Icons.h',header+'}\n');
console.log(`Converted ${names.length} source icons to HordeIcons.ttf`);
