// Runs one script through the wasm build with a CLI-compatible interface, so the
// normal test suite can check the wasm build against perl:
//   make wasm && make wasm-test
import { createRequire } from 'module';
import { readFileSync, readdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
const require = createRequire(import.meta.url);
const createPicoperl = require(join(dirname(fileURLToPath(import.meta.url)), 'picoperl.js'));

const script = process.argv[2];
let code = null;
const mod = await createPicoperl({
  print: (t) => process.stdout.write(t + '\n'),
  printErr: (t) => process.stderr.write(t + '\n'),
  onExit: (c) => { code = c; },
  preRun: [(m) => Object.assign(m.ENV, process.env)],
});

/* recreate the script and its data/module files inside MEMFS under the same
   relative paths, so error messages and @INC behave like the native CLI */
/* run in a directory of its own, not in "/": tests that look at the current directory
   (getcwd, chdir, rel2abs) expect a real path there. The embedded /lib shows up as ./lib. */
mod.FS.mkdir('/work');
mod.FS.symlink('/lib', '/work/lib');
mod.FS.chdir('/work');
const dir = dirname(script);
mod.FS.mkdirTree('/work/' + dir);
mod.FS.writeFile(script, readFileSync(script));
const copyAux = (d) => {
  for (const ent of readdirSync(d, { withFileTypes: true })) {
    const p = join(d, ent.name);
    if (ent.isDirectory()) { mod.FS.mkdirTree('/work/' + p); copyAux(p); }
    else if (/\.(txt|pm)$/.test(ent.name)) mod.FS.writeFile(p, readFileSync(p));
  }
};
copyAux(dir);

try {
  mod.callMain([script]);
} catch (e) {
  if (!(e && e.name === 'ExitStatus')) { process.stderr.write(String(e) + '\n'); process.exit(1); }
}
/* with ASYNCIFY, callMain returns at the first sleep; wait for onExit */
const t0 = Date.now();
while (code === null && Date.now() - t0 < 30000) await new Promise((r) => setTimeout(r, 5));
process.exit(code === null ? 124 : code);
