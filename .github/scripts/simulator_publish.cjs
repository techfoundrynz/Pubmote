const fs = require('node:fs');

const payload = '.pio/incoming-preview';
const read = name => JSON.parse(fs.readFileSync(`${payload}/${name}`, 'utf8'));

async function locate({github, context, core}) {
  if (context.eventName === 'pull_request_target') return;
  const artifacts = await github.paginate(github.rest.actions.listWorkflowRunArtifacts, {
    ...context.repo, run_id: context.payload.workflow_run.id, per_page: 100,
  });
  core.setOutput('available', artifacts.some(a => a.name === 'simulator-preview' && !a.expired));
}

async function validate({github, context, core}) {
  if (context.eventName === 'pull_request_target') {
    core.setOutput('publish', true);
    core.setOutput('destination', `simulator/pr-${context.payload.pull_request.number}`);
    core.setOutput('remove', true);
    return;
  }
  if (!fs.existsSync(`${payload}/build.json`)) return;
  const metadata = read('build.json');
  const run = context.payload.workflow_run;
  if (!/^[a-f0-9]{40}$/.test(metadata.commit)) throw new Error('Invalid preview commit');
  if (metadata.base && !/^[a-f0-9]{40}$/.test(metadata.base)) throw new Error('Invalid comparison base');
  if (metadata.kind === 'pr') {
    if (run.event !== 'pull_request' || metadata.commit !== run.head_sha) throw new Error('PR payload does not match its build');
    const pr = (await github.rest.pulls.get({...context.repo, pull_number: metadata.pr})).data;
    if (pr.base.repo.full_name !== `${context.repo.owner}/${context.repo.repo}` || pr.head.sha !== metadata.commit || pr.state !== 'open') {
      core.info('Skipping a closed or superseded PR preview');
      return;
    }
    core.setOutput('destination', `simulator/pr-${pr.number}/${metadata.commit}`);
  } else if (metadata.kind === 'tag') {
    if (!['push', 'release', 'workflow_dispatch'].includes(run.event)) throw new Error('Tag preview has an unexpected trigger');
    if (!/^[A-Za-z0-9][A-Za-z0-9._/-]*$/.test(metadata.tag) || metadata.tag.split('/').some(part => !part || part === '.' || part === '..')) {
      throw new Error('Invalid preview tag');
    }
    const commit = (await github.rest.repos.getCommit({...context.repo, ref: `refs/tags/${metadata.tag}`})).data.sha;
    if (commit !== metadata.commit || (run.event === 'push' && commit !== run.head_sha)) throw new Error('Tag payload does not match the published tag');
    core.setOutput('destination', `simulator/releases/${metadata.tag}`);
    core.setOutput('immutable', true);
  } else {
    core.info('Manual branch builds are artifact-only');
    return;
  }
  core.setOutput('source', payload);
  core.setOutput('publish', true);
}

async function report({github, context, core}) {
  const metadata = read('build.json');
  const summary = read('report/summary.json');
  const base = process.env.PAGES_URL.replace(/\/$/, '');
  const destination = process.env.PREVIEW_DESTINATION.split('/').map(encodeURIComponent).join('/');
  const url = `${base}/${destination}/`;
  const counts = summary.counts || {};
  const changed = ['changed', 'added', 'removed', 'resized'].reduce((sum, key) => sum + (Number(counts[key]) || 0), 0);
  const marker = '<!-- simulator-preview -->';
  const baseRevision = metadata.base ? ` · base \`${metadata.base.slice(0, 8)}\`` : '';
  const body = `${marker}\n### Simulator preview\n[Open simulator](${url}) · [Visual comparison](${url}report/)\n\nCommit \`${metadata.commit.slice(0, 8)}\`${baseRevision} · ${Number(summary.total)} scenarios · ${changed} visual changes.\n\nRendering and browser interaction tests passed. Visual changes are advisory.\n<!-- /simulator-preview -->`;
  await core.summary.addRaw(body).write();
  if (metadata.kind === 'pr') {
    const comments = await github.paginate(github.rest.issues.listComments, {...context.repo, issue_number: metadata.pr, per_page: 100});
    const previous = comments.find(comment => comment.user.login === 'github-actions[bot]' && comment.body.includes(marker));
    if (previous) await github.rest.issues.updateComment({...context.repo, comment_id: previous.id, body});
    else await github.rest.issues.createComment({...context.repo, issue_number: metadata.pr, body});
  } else {
    let release;
    try { release = (await github.rest.repos.getReleaseByTag({...context.repo, tag: metadata.tag})).data; }
    catch (error) { if (error.status === 404) return; throw error; }
    const existing = (release.body || '').replace(/<!-- simulator-preview -->[\s\S]*?<!-- \/simulator-preview -->/g, '').trimEnd();
    await github.rest.repos.updateRelease({...context.repo, release_id: release.id, body: `${existing}\n\n${body}`});
  }
}

module.exports = {locate, validate, report};
