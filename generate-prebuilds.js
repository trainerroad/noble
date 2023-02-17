var eachSeries = require('each-series-async');
var { createReadStream, createWriteStream, stat } = require('fs');
var { resolve } = require('path');
var tar = require('tar-stream');
const packageInfo = require('./package.json');
var zlib = require('zlib');
var { allTargets } = require('node-abi');
const { spawn } = require('child_process');

function spawnAsync(cmd, args) {
  return new Promise((res) => {
    spawn(cmd, args, { stdio: 'inherit', shell: true }).on('exit', res);
  });
}
function mode(octal) {
  return parseInt(octal, 8);
}

function processTarball(tarballPath) {
  console.log('process tarball', tarballPath);
  return new Promise(async (res) => {
    console.log('starting');
    const tarStream = tar.pack();
    var ws = createWriteStream(tarballPath);
    tarStream.pipe(zlib.createGzip({ level: 9 })).pipe(ws);
    eachSeries(
      ['build/Release/binding.node', 'build/Release/noble.node'],
      function processFile(filename, nextFile) {
        stat(filename, function (err, st) {
          if (err) return nextFile(err);

          var stream = tarStream.entry({
            name: filename
              .replace(/^build\//g, '')
              .replace(/\\/g, '/')
              .replace(/:/g, '_'),
            size: st.size,
            mode: st.mode | mode('444') | mode('222'),
            gid: st.gid,
            uid: st.uid,
          });

          createReadStream(`./${filename}`).pipe(stream).on('finish', nextFile);

          stream.on('error', nextFile);
        });
      },
      function allFilesProcessed(err) {
        tarStream.finalize();
        tarStream.on('close', () => {
          res();
        });
        // res();
      }
    );

    // const tarStream = tar.pack();
    // console.log('create write stream');
    // const ws = createWriteStream(tarballPath);
    // tarStream.pipe(createGzip({ level: 9 })).pipe(ws);

    // for (const file of [
    //   'build/Release/binding.node',
    //   'build/Release/noble.node',
    // ]) {
    //   try {
    //     await processFile(tarStream, file, resolve(`./${file}`));
    //   } catch (err) {
    //     console.log('error processing file', err);
    //     continue;
    //   }
    // }

    // console.log('all files processed finalizing');
    // tarStream.finalize();
    // const extractTar = extract();
    // const packTar = pack();
    // extractTar.on('error', (err) => {
    //   console.log('extract error', err);
    // });
    // packTar.on('error', (err) => {
    //   console.log('pack error', err);
    // });

    // extractTar.on('entry', function (header, stream, callback) {
    //   console.log('entry', header.name);

    //   stream.pipe(packTar.entry(header, callback));
    // });
    // extractTar.on('finish', () => {
    //   packTar.finalize();
    // });
    // const temp = `${tarballPath}.tmp`;
    // await rename(tarballPath, temp);
    // const readStream = createReadStream(temp);
    // const writeStream = createWriteStream(tarballPath);
    // readStream
    //   .pipe(
    //     isGzipped(tarballPath) ? createGunzip({ level: 9 }) : new PassThrough()
    //   )
    //   .pipe(extractTar);
    // packTar
    //   .pipe(
    //     isGzipped(tarballPath) ? createGzip({ level: 9 }) : new PassThrough()
    //   )
    //   .pipe(writeStream);
    // packTar.on('close', () => {
    //   console.log('close called');
    //   resolve();
    // });
  });
}

async function run() {
  const targets = allTargets.filter(
    (x) =>
      (x.runtime === 'node' && parseInt(x.abi, 10) >= 79) ||
      (x.runtime === 'electron' && parseInt(x.abi, 10) > 97)
  );
  for (const { runtime, abi } of targets) {
    console.log('prebuilding', runtime, abi);
    await spawnAsync('prebuild', [`-t ${abi}`, `-r ${runtime}`]);

    const tarballPath = resolve(
      './prebuilds',
      '@trainerroad',
      `noble-v${packageInfo.version}-${runtime}-v${abi}-${process.platform}-${process.arch}.tar.gz`
    );
    try {
      console.log('process tarball');
      await processTarball(tarballPath);
    } catch (err) {
      console.log(err);
    }
    return;
  }
}

run()
  .then(() => {
    process.exit(0);
  })
  .catch((err) => {
    console.log(err);
    process.exit(1);
  });
