import {readFileSync, writeFileSync} from 'fs';
import {applyStampCrc, verifyStampCrc} from './vendor/ioddforge/crc.mjs';
try {
  const [command, file] = process.argv.slice(2);
  const bytes = new Uint8Array(readFileSync(file));
  if (command === 'write') writeFileSync(file, applyStampCrc(bytes).bytes);
  else if (command === 'verify') {
    const result = verifyStampCrc(bytes);
    console.log(JSON.stringify(result));
    if (!result.valid) process.exitCode = 1;
  } else throw new Error('Expected write or verify');
} catch (error) { console.error(error.message); process.exitCode = 1; }
