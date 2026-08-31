/* Runs one picoperl script per Worker, in a fresh wasm instance; the page
 * terminates the worker to stop a runaway script.
 *
 * This file is not loaded directly: index.html downloads picoperl.js once,
 * prepends it to this file and creates the Worker from a Blob URL, so
 * createPicoperl is already defined here. The wasm arrives precompiled as a
 * WebAssembly.Module with the run message - a run makes no network requests.
 *
 * Messages in:  { type: 'run', src, env, wasmModule }
 *               { type: 'input', kind: 'gpio'|'encoder'|'adc', ... }  (widgets;
 *               processed while the script sleeps, thanks to ASYNCIFY)
 * Messages out: { type: 'out'|'err', text }, { type: 'exit', code },
 *               { type: 'error', text }, { type: 'board', ... } (from shim.c) */

let mod = null;

onmessage = async (e) => {
  const m = e.data;
  if (m.type === 'input') {
    if (!mod) return;
    if (m.kind === 'gpio') mod._pp_wasm_gpio_set(m.pin, m.level);
    else if (m.kind === 'encoder') mod._pp_wasm_encoder_add(m.steps);
    else if (m.kind === 'adc') mod._pp_wasm_adc_set(m.pin, m.value);
    return;
  }
  try {
    mod = await createPicoperl({
      print:    (t) => postMessage({ type: 'out', text: t }),
      printErr: (t) => postMessage({ type: 'err', text: t }),
      onExit:   (code) => postMessage({ type: 'exit', code }),
      onAbort:  (what) => postMessage({ type: 'error', text: 'abort: ' + what }),
      preRun:   [(inst) => Object.assign(inst.ENV, m.env || {})],   // before getenv() materializes environ
      instantiateWasm: (imports, done) => {
        const inst = new WebAssembly.Instance(m.wasmModule, imports);
        done(inst, m.wasmModule);
        return inst.exports;
      },
    });
    mod.FS.writeFile('main.pl', m.src);
    mod.callMain(['main.pl']);
    /* with ASYNCIFY, callMain returns as soon as the script first sleeps;
       completion is reported through onExit */
  } catch (err) {
    if (!(err && err.name === 'ExitStatus'))
      postMessage({ type: 'error', text: String(err && err.stack || err) });
  }
};
