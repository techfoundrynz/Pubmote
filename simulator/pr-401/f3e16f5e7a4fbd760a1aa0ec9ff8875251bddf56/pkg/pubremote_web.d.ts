/* tslint:disable */
/* eslint-disable */

export class WebSimulator {
    private constructor();
    free(): void;
    [Symbol.dispose](): void;
    control(name: string, value: any): void;
    invoke(callback: string): void;
    navigate(screen: string): void;
    /**
     * Test hooks use the same properties and callbacks as the real controls.
     */
    state(name: string): any;
}

export function start(base_url: string, width: number, height: number, square: boolean): Promise<WebSimulator>;

export type InitInput = RequestInfo | URL | Response | BufferSource | WebAssembly.Module;

export interface InitOutput {
    readonly memory: WebAssembly.Memory;
    readonly __wbg_websimulator_free: (a: number, b: number) => void;
    readonly start: (a: number, b: number, c: number, d: number, e: number) => any;
    readonly websimulator_control: (a: number, b: number, c: number, d: any) => [number, number];
    readonly websimulator_invoke: (a: number, b: number, c: number) => [number, number];
    readonly websimulator_navigate: (a: number, b: number, c: number) => void;
    readonly websimulator_state: (a: number, b: number, c: number) => any;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___js_sys_47ffc250f120f23d___Array__web_sys_622b2e1811b9a777___features__gen_ResizeObserver__ResizeObserver______true_: (a: number, b: number, c: any, d: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___js_sys_47ffc250f120f23d___Function_fn_wasm_bindgen_5849ac1000262468___JsValue_____wasm_bindgen_5849ac1000262468___sys__Undefined___js_sys_47ffc250f120f23d___Function_fn_wasm_bindgen_5849ac1000262468___JsValue_____wasm_bindgen_5849ac1000262468___sys__Undefined_______true_: (a: number, b: number, c: any, d: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue__core_608f92abc48d28da___result__Result_____wasm_bindgen_5849ac1000262468___JsError___true_: (a: number, b: number, c: any) => [number, number];
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___core_608f92abc48d28da___option__Option_web_sys_622b2e1811b9a777___features__gen_Blob__Blob_______true_: (a: number, b: number, c: number) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true_: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__12: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__14: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__17: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__18: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__19: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__21: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___wasm_bindgen_5849ac1000262468___JsValue______true__9: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true_: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true__11: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true__13: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true__15: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true__16: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke___web_sys_622b2e1811b9a777___features__gen_FocusEvent__FocusEvent______true__20: (a: number, b: number, c: any) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke_______true_: (a: number, b: number) => void;
    readonly wasm_bindgen_5849ac1000262468___convert__closures_____invoke_______true__1_: (a: number, b: number) => void;
    readonly __wbindgen_malloc: (a: number, b: number) => number;
    readonly __wbindgen_realloc: (a: number, b: number, c: number, d: number) => number;
    readonly __externref_table_alloc: () => number;
    readonly __wbindgen_externrefs: WebAssembly.Table;
    readonly __wbindgen_exn_store: (a: number) => void;
    readonly __wbindgen_free: (a: number, b: number, c: number) => void;
    readonly __wbindgen_destroy_closure: (a: number, b: number) => void;
    readonly __externref_table_dealloc: (a: number) => void;
    readonly __wbindgen_start: () => void;
}

export type SyncInitInput = BufferSource | WebAssembly.Module;

/**
 * Instantiates the given `module`, which can either be bytes or
 * a precompiled `WebAssembly.Module`.
 *
 * @param {{ module: SyncInitInput }} module - Passing `SyncInitInput` directly is deprecated.
 *
 * @returns {InitOutput}
 */
export function initSync(module: { module: SyncInitInput } | SyncInitInput): InitOutput;

/**
 * If `module_or_path` is {RequestInfo} or {URL}, makes a request and
 * for everything else, calls `WebAssembly.instantiate` directly.
 *
 * @param {{ module_or_path: InitInput | Promise<InitInput> }} module_or_path - Passing `InitInput` directly is deprecated.
 *
 * @returns {Promise<InitOutput>}
 */
export default function __wbg_init (module_or_path?: { module_or_path: InitInput | Promise<InitInput> } | InitInput | Promise<InitInput>): Promise<InitOutput>;
