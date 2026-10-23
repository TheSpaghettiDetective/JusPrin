// Release checks for .github/workflows/jusprin_release.yml.
//
//   node release.mjs version <Version.hpp>
//       Prints JusPrin's version, the one in Version.hpp.
//   node release.mjs check <Version.hpp> <releases.json> <tags.txt>
//       Reads GitHub's release list (drafts included, as an authenticated
//       request returns it) and the repository's tags, one refs/tags/... per
//       line. Fails if this version was already published, is not above
//       every published JusPrin version, or its tag already exists. Prints
//       key=value lines for $GITHUB_OUTPUT: version, tag, prerelease,
//       draft_id.
//
// The update check (ReleaseFeed.cpp) reads tags vX.Y.Z and jusprin/vX.Y.Z.
// New releases are tagged jusprin/vX.Y.Z, as JusPrin 1.4.0 was: the
// repository still holds OrcaSlicer's own tags v2.0.0 to v2.3.1, and
// publishing a draft whose tag exists attaches it to that tag's commit.

import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const SEMVER = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z.-]+)?$/;

export function parseVersion(text) {
    const match = SEMVER.exec(text);
    if (!match)
        return null;
    return {
        text,
        core: [Number(match[1]), Number(match[2]), Number(match[3])],
        prerelease: match[4] ? match[4].split('.') : [],
    };
}

// Semantic Versioning 2.0.0 precedence: a version with a pre-release part
// ranks below the same version without one.
export function compareVersions(a, b) {
    for (let i = 0; i < 3; ++i)
        if (a.core[i] !== b.core[i])
            return a.core[i] < b.core[i] ? -1 : 1;
    if (a.prerelease.length === 0 || b.prerelease.length === 0)
        return b.prerelease.length - a.prerelease.length;
    for (let i = 0; i < Math.min(a.prerelease.length, b.prerelease.length); ++i) {
        const x = a.prerelease[i], y = b.prerelease[i];
        const xn = /^\d+$/.test(x), yn = /^\d+$/.test(y);
        if (x === y)
            continue;
        if (xn && yn)
            return Number(x) < Number(y) ? -1 : 1;
        if (xn !== yn)
            return xn ? -1 : 1;
        return x < y ? -1 : 1;
    }
    return a.prerelease.length - b.prerelease.length;
}

export function tagVersion(tag) {
    let version = tag.startsWith('jusprin/') ? tag.slice('jusprin/'.length) : tag;
    if (version.startsWith('v'))
        version = version.slice(1);
    return parseVersion(version);
}

export function readVersion(header) {
    const match = /kVersion\[\]\s*=\s*"([^"]*)"/.exec(header);
    if (!match)
        throw new Error('Version.hpp has no kVersion = "X.Y.Z" line');
    const version = parseVersion(match[1]);
    if (!version)
        throw new Error(`Version.hpp's version "${match[1]}" is not X.Y.Z or X.Y.Z-label`);
    return version;
}

export function checkRelease(version, releases, tags = []) {
    const tag = `jusprin/v${version.text}`;
    // A draft has no tag until it is published, so an existing one belongs to
    // something else.
    if (tags.includes(`refs/tags/${tag}`))
        throw new Error(`The tag ${tag} already exists. JusPrin ${version.text} would be published on its commit.`);
    let draftId = '';
    for (const release of releases) {
        const released = tagVersion(release.tag_name ?? '');
        if (!released || compareVersions(released, version) !== 0)
            continue;
        if (!release.draft)
            throw new Error(`JusPrin ${version.text} is already released (${release.tag_name}). Raise the version in Version.hpp.`);
        if (release.tag_name === tag) {
            if (draftId)
                throw new Error(`There are two drafts tagged ${tag}. Delete one.`);
            draftId = String(release.id);
        }
    }
    for (const release of releases) {
        if (release.draft)
            continue;
        const released = tagVersion(release.tag_name ?? '');
        if (released && compareVersions(released, version) > 0)
            throw new Error(`JusPrin ${version.text} is below the released ${released.text} (${release.tag_name}).`);
    }
    return { version: version.text, tag, prerelease: version.prerelease.length > 0, draftId };
}

function main(argv) {
    const [command, headerPath, releasesPath, tagsPath] = argv;
    const version = readVersion(readFileSync(headerPath, 'utf8'));
    if (command === 'version') {
        console.log(version.text);
        return;
    }
    if (command === 'check') {
        const releases = JSON.parse(readFileSync(releasesPath, 'utf8')).flat();
        if (!tagsPath)
            throw new Error('check needs the tag list too');
        const tags = readFileSync(tagsPath, 'utf8').split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
        const result = checkRelease(version, releases, tags);
        console.log(`version=${result.version}`);
        console.log(`tag=${result.tag}`);
        console.log(`prerelease=${result.prerelease}`);
        console.log(`draft_id=${result.draftId}`);
        return;
    }
    throw new Error(`unknown command "${command}"`);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
    try {
        main(process.argv.slice(2));
    } catch (error) {
        console.error(`::error::${error.message}`);
        process.exit(1);
    }
}
