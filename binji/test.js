const environ = {
  USER : 'alice',
};
const argv = ['clang', ...arguments];
let memfsmem;
let clangmem;


class ProcExit {
  constructor(code) { this.msg = `process exited with code ${code}.`; }
  toString() { return this.msg; }
};

class NotImplemented extends Error {
  constructor(modname, fieldname) {
    super(`${modname}.${fieldname} not implemented.`);
  }
}

class AbortError extends Error {
  constructor(msg = 'abort') { super(msg); }
}

class AssertError extends Error {
  constructor(msg) { super(msg); }
}

function assert(cond) {
  if (!cond) {
    throw new AssertError('assertion failed.');
  }
}

function getModule(filename) {
  return new WebAssembly.Module(readbuffer(filename));
}

function getInstance(filename, imports) {
  const mod = getModule(filename);
  return new WebAssembly.Instance(mod, imports);
}

const ESUCCESS = 0;

class Memory {
  constructor(memory) {
    this.memory = memory;
    this.buffer = this.memory.buffer;
    this.u8 = new Uint8Array(this.buffer);
    this.u32 = new Uint32Array(this.buffer);
  }

  check() {
    if (this.buffer.byteLength === 0) {
      this.buffer = this.memory.buffer;
      this.u8 = new Uint8Array(this.buffer);
      this.u32 = new Uint32Array(this.buffer);
    }
  }

  read8(o) { return this.u8[o]; }
  read32(o) { return this.u32[o >> 2]; }
  write8(o, v) { this.u8[o] = v; }
  write32(o, v) { this.u32[o >> 2] = v; }
  write64(o, vlo, vhi = 0) { this.write32(o, vlo); this.write32(o + 4, vhi); }

  readStr(o, len = -1) {
    let str = '';
    let end = this.buffer.byteLength;
    if (len != -1)
      end = o + len;
    for (let i = o; i < end && this.read8(i) != 0; ++i)
      str += String.fromCharCode(this.read8(i));
    return str;
  }

  writeStr(o, str) {
    for (let i = 0; i < str.length; i++) {
      const c = str.charCodeAt(i);
      assert(c < 256);
      this.write8(o++, c);
    }
    this.write8(o++, 0);
    return str.length + 1;
  }
};

class HostWriteBuffer {
  constructor() {
    this.buffer = '';
  }

  write(str) {
    this.buffer += str;
    while (true) {
      const newline = this.buffer.indexOf('\n');
      if (newline === -1) {
        break;
      }
      print(this.buffer.slice(0, newline));
      this.buffer = this.buffer.slice(newline + 1);
    }
  }

  flush() {
    print(this.buffer);
  }
}

const host_write_buffer = new HostWriteBuffer();

const env = {
  abort : function() {
    throw new AbortError();
  },
  host_write : function(fd, iovs, iovs_len, nwritten_out) {
    clangmem.check();
    assert(fd <= 2);
    let size = 0;
    let str = '';
    for (let i = 0; i < iovs_len; ++i) {
      const buf = clangmem.read32(iovs);
      iovs += 4;
      const len = clangmem.read32(iovs);
      iovs += 4;
      str += clangmem.readStr(buf, len);
      size += len;
    }
    clangmem.write32(nwritten_out, size);
    host_write_buffer.write(str);
    return ESUCCESS;
  },
  memfs_log : function(buf, len) {
    memfsmem.check();
    print(memfsmem.readStr(buf, len));
  },
  copy_out : function(clang_dst, memfs_src, size) {
    clangmem.check();
    const dst = new Uint8Array(clangmem.buffer, clang_dst, size);
    memfsmem.check();
    const src = new Uint8Array(memfsmem.buffer, memfs_src, size);
    // print(`copy_out(${clang_dst.toString(16)}, ${memfs_src.toString(16)}, ${size})`);
    dst.set(src);
  },
  copy_in : function(memfs_dst, clang_src, size) {
    memfsmem.check();
    const dst = new Uint8Array(memfsmem.buffer, memfs_dst, size);
    clangmem.check();
    const src = new Uint8Array(clangmem.buffer, clang_src, size);
    // print(`copy_in(${memfs_dst.toString(16)}, ${clang_src.toString(16)}, ${size})`);
    dst.set(src);
  },
};

const memfs = getInstance('memfs', {env});
memfsmem = new Memory(memfs.exports.memory);
print('initializing memfs...');
memfs.exports.init();
print('done.');


const wasi_unstable = {
  proc_exit: function(code) {
    throw new ProcExit(code);
  },
  environ_sizes_get: function(environ_count_out, environ_buf_size_out) {
    clangmem.check();
    let size = 0;
    const names = Object.getOwnPropertyNames(environ);
    for (const name of names) {
      const value = environ[name];
      // +2 to account for = and \0 in "name=value\0".
      size += name.length + value.length + 2;
    }
    clangmem.write64(environ_count_out, names.length);
    clangmem.write64(environ_buf_size_out, size);
    return ESUCCESS;
  },
  environ_get: function(environ_ptrs, environ_buf) {
    clangmem.check();
    const names = Object.getOwnPropertyNames(environ);
    for (const name of names) {
      clangmem.write32(environ_ptrs, environ_buf);
      environ_ptrs += 4;
      environ_buf += clangmem.writeStr(environ_buf, `${name}=${environ[name]}`);
    }
    clangmem.write32(environ_ptrs, 0);
    return ESUCCESS;
  },
  args_sizes_get: function(argc_out, argv_buf_size_out) {
    clangmem.check();
    let size = 0;
    for (let arg of argv) {
      size += arg.length + 1;  // "arg\0".
    }
    clangmem.write64(argc_out, argv.length);
    clangmem.write64(argv_buf_size_out, size);
    return ESUCCESS;
  },
  args_get: function(argv_ptrs, argv_buf) {
    clangmem.check();
    for (let arg of argv) {
      clangmem.write32(argv_ptrs, argv_buf);
      argv_ptrs += 4;
      argv_buf += clangmem.writeStr(argv_buf, arg);
    }
    clangmem.write32(argv_ptrs, 0);
    return ESUCCESS;
  },
  random_get: function(buf, buf_len) {
    const data = new Uint8Array(clangmem.buffer, buf, buf_len);
    for (let i = 0; i < buf_len; ++i) {
      data[i] = (Math.random() * 256) | 0;
    }
  },
  clock_time_get: function(clock_id, precision, time_out) {
    throw new NotImplemented('wasi_unstable', 'clock_time_get');
  },
  poll_oneoff: function(in_ptr, out_ptr, nsubscriptions, nevents_out) {
    throw new NotImplemented('wasi_unstable', 'poll_oneoff');
  }
};

// Fill in some WASI implementations from memfs.
Object.assign(wasi_unstable, memfs.exports);

const clang = getInstance('clang', {wasi_unstable});
clangmem = new Memory(clang.exports.memory);

print('running...');
clang.exports._start();
host_write_buffer.flush();
print('done.');
