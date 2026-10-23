// node --test src/slic3r/GUI/JusPrin/Packaging/release.test.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { checkRelease, compareVersions, parseVersion, readVersion, tagVersion } from './release.mjs';

const v = (text) => parseVersion(text);
const release = (tag_name, extra = {}) => ({ id: 1, tag_name, draft: false, prerelease: false, ...extra });

// JusPrin's releases on GitHub as of 2026-10-22, newest first.
const published = [
    release('nightly-builds', { prerelease: true }),
    release('jusprin/v1.4.0'),
    release('v1.3.0'),
    release('v1.2.0'),
    release('v0.2.0'),
];

test('reads the version in the checked-in Version.hpp', () => {
    const header = readFileSync(new URL('../Updates/Version.hpp', import.meta.url), 'utf8');
    assert.match(readVersion(header).text, /^\d+\.\d+\.\d+/);
});

test('refuses a Version.hpp without a usable version', () => {
    assert.throws(() => readVersion('inline constexpr char kOther[] = "2.0.0";'));
    assert.throws(() => readVersion('inline constexpr char kVersion[] = "2.0";'));
});

test('orders versions as Semantic Versioning does', () => {
    assert.ok(compareVersions(v('2.0.0'), v('1.4.0')) > 0);
    assert.ok(compareVersions(v('2.0.0-beta1'), v('2.0.0')) < 0);
    assert.ok(compareVersions(v('2.0.0-beta.2'), v('2.0.0-beta.10')) < 0);
    assert.ok(compareVersions(v('2.0.0-beta'), v('2.0.0-beta.1')) < 0);
    assert.ok(compareVersions(v('1.10.0'), v('1.9.0')) > 0);
    assert.equal(compareVersions(v('2.0.0'), v('2.0.0')), 0);
});

test('reads both tag forms and ignores other tags', () => {
    assert.equal(tagVersion('jusprin/v1.4.0').text, '1.4.0');
    assert.equal(tagVersion('v1.3.0').text, '1.3.0');
    assert.equal(tagVersion('nightly-builds'), null);
});

// Some of the repository's tags as of 2026-10-23: OrcaSlicer's own release
// tags came with the fork.
const tags = ['refs/tags/jusprin/v1.4.0', 'refs/tags/v1.3.0', 'refs/tags/v2.0.0', 'refs/tags/v2.0.0-beta',
              'refs/tags/v2.3.1', 'refs/tags/nightly-builds'];

test('2.0.0 can be drafted over the 1.x releases', () => {
    assert.deepEqual(checkRelease(v('2.0.0'), published, tags),
                     { version: '2.0.0', tag: 'jusprin/v2.0.0', prerelease: false, draftId: '' });
});

test('an existing tag for the version is refused', () => {
    assert.throws(() => checkRelease(v('2.0.0'), published, [...tags, 'refs/tags/jusprin/v2.0.0']),
                  /tag jusprin\/v2.0.0 already exists/);
});

test('a draft made for the old vX.Y.Z tag is not reused', () => {
    const releases = [release('v2.0.0', { id: 7, draft: true }), ...published];
    assert.equal(checkRelease(v('2.0.0'), releases, tags).draftId, '');
});

test('a test version is drafted as a pre-release', () => {
    assert.equal(checkRelease(v('2.1.0-beta1'), published).prerelease, true);
});

test('an existing draft for the tag is reused', () => {
    const releases = [release('jusprin/v2.0.0', { id: 42, draft: true }), ...published];
    assert.equal(checkRelease(v('2.0.0'), releases).draftId, '42');
});

test('a released version is never built again', () => {
    assert.throws(() => checkRelease(v('1.4.0'), published), /already released/);
    assert.throws(() => checkRelease(v('2.0.0'), [release('v2.0.0'), ...published]), /already released/);
});

test('a version below a published one is refused', () => {
    assert.throws(() => checkRelease(v('1.3.5'), published), /below the released 1.4.0/);
    assert.throws(() => checkRelease(v('2.0.0-rc1'), [release('v2.0.0'), ...published]), /below/);
});

test('two drafts for one tag are refused', () => {
    const releases = [release('jusprin/v2.0.0', { id: 1, draft: true }), release('jusprin/v2.0.0', { id: 2, draft: true })];
    assert.throws(() => checkRelease(v('2.0.0'), releases), /two drafts/);
});
