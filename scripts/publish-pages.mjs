import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Publish the committed standalone artifact without changing the active checkout.
// This updates gh-pages with a normal fast-forward push; it never force-pushes.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const git = (args, options = {}) => execFileSync('git', args, {
  cwd: root, maxBuffer: 16 * 1024 * 1024, ...options,
});
const text = (args) => git(args).toString('utf8').trim();
const repository = 'Cheng-xiu/ustc-danmaku';
const sourceCommit = text(['rev-parse', 'HEAD']);
const html = readFileSync(path.join(root, 'demo/ustc-danmaku.html'));
if (!html.equals(git(['show', 'HEAD:demo/ustc-danmaku.html']))) {
  throw new Error('demo/ustc-danmaku.html has uncommitted changes. Commit the tested artifact first.');
}
const remote = text(['remote', 'get-url', 'origin']);
if (!/^(?:https:\/\/github\.com\/|git@github\.com:)Cheng-xiu\/ustc-danmaku(?:\.git)?$/.test(remote)) {
  throw new Error('origin must point to Cheng-xiu/ustc-danmaku.');
}

const existing = text(['ls-remote', 'origin', 'refs/heads/gh-pages']);
let parent;
if (existing) {
  git(['fetch', '--no-tags', 'origin', 'refs/heads/gh-pages'], { stdio: ['ignore', 'pipe', 'inherit'] });
  parent = text(['rev-parse', 'FETCH_HEAD']);
  // Refuse to replace an unrelated website on a branch with the same name.
  const previous = JSON.parse(text(['show', `${parent}:deployment.json`]));
  if (previous.repository !== repository || previous.publisher !== 'scripts/publish-pages.mjs') {
    throw new Error('gh-pages is not owned by this publisher. Check its existing site first.');
  }
  const allowed = new Set(['index.html', '.nojekyll', '.gitattributes', 'deployment.json']);
  const files = text(['ls-tree', '-r', '--name-only', parent]).split('\n');
  if (files.some((file) => !allowed.has(file))) {
    throw new Error('gh-pages has additional files. Review them before publishing.');
  }
}

const htmlSha256 = createHash('sha256').update(html).digest('hex');
const deployment = {
  repository, publisher: 'scripts/publish-pages.mjs', source_commit: sourceCommit,
  html_sha256: htmlSha256, html_bytes: html.length,
};
const files = new Map([
  ['index.html', html],
  ['.nojekyll', Buffer.alloc(0)],
  ['.gitattributes', Buffer.from('index.html -text -diff\n')],
  ['deployment.json', Buffer.from(JSON.stringify(deployment, null, 2) + '\n')],
]);
const entries = [...files].map(([name, content]) => {
  const sha = git(['hash-object', '-w', '--stdin'], { input: content }).toString('utf8').trim();
  return `100644 blob ${sha}\t${name}\n`;
}).join('');
const tree = git(['mktree'], { input: entries }).toString('utf8').trim();
if (parent && text(['rev-parse', `${parent}^{tree}`]) === tree) {
  console.log(JSON.stringify({ ...deployment, commit: parent, unchanged: true }, null, 2));
} else {
  const args = ['commit-tree', tree, '-m', `发布网页 Demo ${sourceCommit.slice(0, 7)}`];
  if (parent) args.push('-p', parent);
  const commit = text(args);
  git(['push', 'origin', `${commit}:refs/heads/gh-pages`], { stdio: ['ignore', 'pipe', 'inherit'] });
  console.log(JSON.stringify({ ...deployment, commit, unchanged: false }, null, 2));
}
