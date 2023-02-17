import { allTargets } from 'node-abi';

import { spawn } from 'child_process';
import packageInfo from './package.json' assert { type: 'json' };
import { resolve } from 'path';
import tar from 'tar-stream';
import { createReadStream, createWriteStream } from 'fs';
import { rename, unlink } from 'fs/promises';
import { createGunzip, createGzip } from 'zlib';
import { PassThrough } from 'stream';

function spawnAsync(cmd, args) {
  return new Promise((res) => {
    let output = '';
    function onData(data) {
      if (Buffer.isBuffer(data)) {
        output += data.toString();
      } else if (typeof data === 'string') {
        output += data;
      }
    }
    const handler = spawn(cmd, args, { shell: true });
    handler.stderr.on('data', onData);
    handler.stdout.on('data', onData);
    handler.on('exit', (code) => {
      if (code === 0) {
        res(0);
      } else {
        res(output);
      }
    });
  });
}

function isGzipped(fileName) {
  return ['tgz', 'gz', 'gzip'].includes(fileName.split('.').pop());
}

function processTarball(tarballPath) {
  console.log(
    'processing tarball: removing build directory from paths',
    tarballPath
  );
  const temp = `${tarballPath}.tmp`;
  return new Promise(async (resolve) => {
    const extractTar = tar.extract();
    const packTar = tar.pack();
    extractTar.on('entry', function (header, stream, callback) {
      header.name = header.name.replace(/^build\//g, '');
      stream.pipe(packTar.entry(header, callback));
    });
    extractTar.on('finish', async () => {
      packTar.finalize();
      await unlink(temp);
    });
    await rename(tarballPath, temp);
    const readStream = createReadStream(temp);
    const writeStream = createWriteStream(tarballPath);
    readStream
      .pipe(
        isGzipped(tarballPath) ? createGunzip({ level: 9 }) : new PassThrough()
      )
      .pipe(extractTar);
    packTar
      .pipe(
        isGzipped(tarballPath) ? createGzip({ level: 9 }) : new PassThrough()
      )
      .pipe(writeStream);
    writeStream.on('finish', () => {
      resolve();
    });
  });
}

async function prebuild(runtime, abi, arch, version) {
  console.log('prebuilding', process.platform, runtime, abi, arch, version);
  const code = await spawnAsync('prebuild', [
    `-t ${abi}`,
    `-r ${runtime}`,
    `-a ${arch}`,
  ]);

  if (code !== 0) {
    console.log(
      `Error building ${process.platform} ${runtime} v${abi} ${version}`
    );
    console.log(code);
    return;
  }

  const tarballPath = resolve(
    './prebuilds',
    '@trainerroad',
    `noble-v${packageInfo.version}-${runtime}-v${abi}-${process.platform}-${arch}.tar.gz`
  );
  await processTarball(tarballPath);
  console.log('done with tarball');
}

async function run() {
  const targets = allTargets.filter(
    (x) =>
      (x.runtime === 'node' && parseInt(x.abi, 10) >= 79) ||
      (x.runtime === 'electron' &&
        parseInt(x.abi, 10) > 97 &&
        parseInt(x.abi, 10) < 114)
  );
  console.log('total targets', targets.length);
  for (const { runtime, abi, version } of targets) {
    await prebuild(runtime, abi, process.arch, version);

    if (runtime === 'electron') {
      if (process.platform === 'win32') {
        await prebuild(runtime, abi, 'ia32', version);
      } else if (process.platform === 'darwin') {
        // need both arm64 and x64 binaries to support apple silicon
        await prebuild(
          runtime,
          abi,
          process.arch === 'arm64' ? 'x64' : 'arm64',
          version
        );
      }
    }
  }
}

run()
  .then(() => {
    process.exit(0);
  })
  .catch((e) => {
    console.log('error', e);
    process.exit(1);
  });
