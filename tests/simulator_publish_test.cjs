const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');
const publish = require('../.github/scripts/simulator_publish.cjs');

const sha = 'a'.repeat(40);
async function fixture(metadata, task) {
  const original = process.cwd();
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'simulator-publisher-test-'));
  try {
    process.chdir(root);
    fs.mkdirSync('.pio/incoming-preview/report', {recursive: true});
    fs.writeFileSync('.pio/incoming-preview/build.json', JSON.stringify(metadata));
    fs.writeFileSync('.pio/incoming-preview/report/summary.json', JSON.stringify({total: 51, counts: {changed: 2, identical: 49}}));
    await task();
  } finally {
    process.chdir(original);
    if (path.dirname(path.resolve(root)) !== path.resolve(os.tmpdir())) throw new Error('Unexpected test directory');
    fs.rmSync(root, {recursive: true, force: true});
  }
}
function core(outputs) { return {setOutput: (name,value) => outputs[name] = value, info: () => {}}; }
const context = {eventName: 'workflow_run', repo: {owner: 'owner', repo: 'repo'},
  payload: {workflow_run: {event: 'pull_request', head_sha: sha}}};

test('publishes the current PR and skips a superseded revision', async () => {
  await fixture({kind: 'pr', pr: 12, commit: sha}, async () => {
    const outputs = {};
    const pr = {number: 12, head: {sha}, base: {repo: {full_name: 'owner/repo'}}, state: 'open'};
    const github = {rest: {pulls: {get: async () => ({data: pr})}}};
    await publish.validate({github, context, core: core(outputs)});
    assert.equal(outputs.publish, true); assert.equal(outputs.destination, `simulator/pr-12/${sha}`);
    pr.head.sha = 'b'.repeat(40);
    const stale = {};
    await publish.validate({github, context, core: core(stale)});
    assert.equal(stale.publish, undefined);
  });
});

test('rejects forged release payloads and tag SHA mismatches', async () => {
  await fixture({kind: 'tag', tag: 'v1', commit: sha}, async () => {
    await assert.rejects(publish.validate({github: {}, context, core: core({})}), /unexpected trigger/);
    const tagContext = {...context, payload: {workflow_run: {event: 'push', head_sha: sha}}};
    const github = {rest: {repos: {getCommit: async () => ({data: {sha: 'b'.repeat(40)}})}}};
    await assert.rejects(publish.validate({github, context: tagContext, core: core({})}), /does not match/);
    github.rest.repos.getCommit = async () => ({data: {sha}});
    const outputs = {};
    await publish.validate({github, context: tagContext, core: core(outputs)});
    assert.equal(outputs.immutable, true); assert.equal(outputs.destination, 'simulator/releases/v1');
  });
});

test('closed PR cleanup uses the trusted event number', async () => {
  const outputs = {};
  await publish.validate({github: {}, core: core(outputs),
    context: {eventName: 'pull_request_target', payload: {pull_request: {number: 15}}}});
  assert.equal(outputs.destination, 'simulator/pr-15'); assert.equal(outputs.remove, true);
});

test('release reporting preserves existing notes and replaces its own footer', async () => {
  await fixture({kind: 'tag', tag: 'v1', commit: sha}, async () => {
    const oldUrl = process.env.PAGES_URL, oldDestination = process.env.PREVIEW_DESTINATION;
    process.env.PAGES_URL = 'https://pubmote.com/';
    process.env.PREVIEW_DESTINATION = 'simulator/releases/v1';
    try {
      let updated;
      const github = {rest: {repos: {
        getReleaseByTag: async () => ({data: {id: 1, body: 'Original notes\n<!-- simulator-preview -->old<!-- /simulator-preview -->'}}),
        updateRelease: async args => { updated = args.body; },
      }}};
      await publish.report({github, context, core: {summary: {addRaw: () => ({write: async () => {}})}}});
      assert(updated.startsWith('Original notes'));
      assert.equal((updated.match(/<!-- simulator-preview -->/g) || []).length, 1);
      assert(updated.includes('https://pubmote.com/simulator/releases/v1/'));
    } finally {
      if (oldUrl === undefined) delete process.env.PAGES_URL; else process.env.PAGES_URL = oldUrl;
      if (oldDestination === undefined) delete process.env.PREVIEW_DESTINATION; else process.env.PREVIEW_DESTINATION = oldDestination;
    }
  });
});
