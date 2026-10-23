'use strict';

const fs = require('node:fs');
const {createRequire} = require('node:module');
const path = require('node:path');

// Resolve next to the CLI for both local and global npm installations.
const cliRequire = createRequire(require.main?.filename || __filename);
const {getHooks} = cliRequire('@diplodoc/cli/lib/program');
const yaml = cliRequire('js-yaml');

class Extension {
    apply(program) {
        // CLI 5.56.0 embeds a Page Constructor runtime incompatible with its
        // current React client. Emit the compatible, explicitly pinned runtime.
        getHooks(program).AfterAnyRun.tapPromise(
            {name: 'YTsaurusLandingRuntime', before: 'BuildContentMap'},
            async (run) => {
                // md2md does not discover font URLs inside custom CSS. Ship the
                // same font files and license in both publication formats.
                await run.copy(
                    path.join(run.input, 'ru/_assets/fonts'),
                    path.join(run.output, 'ru/_assets/fonts'),
                );
                // Viewer rejects standalone font assets. Keep editable font
                // files in the source and embed them in the emitted stylesheet.
                const stylePath = path.join(run.output, 'ru/_assets/style/landing.css');
                let style = await run.read(stylePath);
                for (const weight of ['Regular', 'Medium', 'SemiBold', 'Bold']) {
                    const filename = `Manrope-${weight}.ttf`;
                    const font = fs.readFileSync(path.join(run.input, 'ru/_assets/fonts', filename));
                    style = style.replaceAll(
                        `url('../fonts/${filename}')`,
                        `url('data:font/ttf;base64,${font.toString('base64')}')`,
                    );
                }
                await run.write(stylePath, style, true);
                if (run.config.outputFormat === 'md') {
                    // Publishing consumes built resources and must not try to
                    // load this source-only build extension from the artifact.
                    const configPath = path.join(run.output, '.yfm');
                    const config = yaml.load(await run.read(configPath));
                    const extensions = (config.extensions || []).filter((extension) => {
                        const name = typeof extension === 'string' ? extension : extension.name;
                        return path.basename(name) !== 'landing-runtime.cjs';
                    });
                    if (extensions.length) {
                        config.extensions = extensions;
                    } else {
                        delete config.extensions;
                    }
                    await run.write(configPath, yaml.dump(config, {lineWidth: -1}), true);
                }
                for (const [entry, filename] of [
                    ['runtime', 'page-constructor-extension.js'],
                    ['styles', 'page-constructor-extension.css'],
                ]) {
                    const content = fs.readFileSync(
                        cliRequire.resolve(`@diplodoc/page-constructor-extension/${entry}`),
                        'utf8',
                    );
                    await run.write(path.join(run.output, '_bundle', filename), content, true);
                }
            },
        );
    }
}

module.exports = {Extension};
