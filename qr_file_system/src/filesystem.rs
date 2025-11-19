use std::ffi::OsStr;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::sync::Arc;
use std::time::SystemTime;

use fuser::{
    FileAttr, FileType, Filesystem, KernelConfig, ReplyAttr, ReplyData, ReplyDirectory, ReplyEntry,
    ReplyOpen, Request,
};
use libc::{c_int, EIO, EISDIR, ENOENT};
use std::time::Duration;

/// Lee un u32 little-endian desde un slice de 4 bytes.
fn u32le_from(buf: &[u8]) -> u32 {
    let mut b = [0u8; 4];
    b.copy_from_slice(&buf[0..4]);
    u32::from_le_bytes(b)
}

#[derive(Debug)]
struct Superblock {
    version: u32,
    block_size: u32,
    total_blocks: u32,
    total_inodes: u32,
    inode_bitmap: [u8; 128],
    data_bitmap: [u8; 128],
    root_inode: u32,
    inode_bitmap_start: u32,
    inode_bitmap_blocks: u32,
    data_bitmap_start: u32,
    data_bitmap_blocks: u32,
    inode_table_start: u32,
    inode_table_blocks: u32,
    data_region_start: u32,
}

#[derive(Debug, Clone)]
struct DiskInode {
    inode_number: u32,
    inode_mode: u32,
    user_id: u32,
    group_id: u32,
    links: u32,
    size: u32,
    direct: [u32; 12],
    indirect1: u32,
}

impl DiskInode {
    fn from_bytes(buf: &[u8]) -> Self {
        let inode_number = u32le_from(&buf[0..4]);
        let inode_mode = u32le_from(&buf[4..8]);
        let user_id = u32le_from(&buf[8..12]);
        let group_id = u32le_from(&buf[12..16]);
        let links = u32le_from(&buf[16..20]);
        let size = u32le_from(&buf[20..24]);
        let mut direct = [0u32; 12];
        for i in 0..12 {
            direct[i] = u32le_from(&buf[24 + i * 4..24 + i * 4 + 4]);
        }
        let indirect1 = u32le_from(&buf[72..76]);
        DiskInode {
            inode_number,
            inode_mode,
            user_id,
            group_id,
            links,
            size,
            direct,
            indirect1,
        }
    }

    fn is_dir(&self) -> bool {
        const S_IFDIR: u32 = 0o040000;
        (self.inode_mode & S_IFDIR) == S_IFDIR
    }
}

#[derive(Debug)]
struct DirEntry {
    inode: u32,
    name: String,
}

pub struct QRFileSystem {
    folder: PathBuf,
    sb: Arc<Superblock>,
    inodes: Arc<Vec<DiskInode>>,
}

impl QRFileSystem {
    /// Crea la estructura QrFs leyendo el bloque 0 y la tabla de inodos.
    pub fn from_folder(folder: PathBuf) -> Result<Self, String> {
        let block0_path = folder.join(format!("block_{:04}.png", 0));
        let mut f = File::open(&block0_path).map_err(|e| format!("open block0: {}", e))?;
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).map_err(|e| format!("read block0: {}", e))?;
        if buf.len() < 308 {
            return Err("block0 demasiado pequeño".to_string());
        }
        if &buf[0..4] != b"QRFS" {
            return Err("magic QRFS no encontrado en block0".to_string());
        }

        let version = u32le_from(&buf[4..8]);
        let block_size = u32le_from(&buf[8..12]);
        let total_blocks = u32le_from(&buf[12..16]);
        let total_inodes = u32le_from(&buf[16..20]);

        let mut inode_bitmap = [0u8; 128];
        inode_bitmap.copy_from_slice(&buf[20..20 + 128]);
        let mut data_bitmap = [0u8; 128];
        data_bitmap.copy_from_slice(&buf[148..148 + 128]);

        let root_inode = u32le_from(&buf[276..280]);

        let inode_bitmap_start = u32le_from(&buf[280..284]);
        let inode_bitmap_blocks = u32le_from(&buf[284..288]);
        let data_bitmap_start = u32le_from(&buf[288..292]);
        let data_bitmap_blocks = u32le_from(&buf[292..296]);
        let inode_table_start = u32le_from(&buf[296..300]);
        let inode_table_blocks = u32le_from(&buf[300..304]);
        let data_region_start = u32le_from(&buf[304..308]);

        let sb = Superblock {
            version,
            block_size,
            total_blocks,
            total_inodes,
            inode_bitmap,
            data_bitmap,
            root_inode,
            inode_bitmap_start,
            inode_bitmap_blocks,
            data_bitmap_start,
            data_bitmap_blocks,
            inode_table_start,
            inode_table_blocks,
            data_region_start,
        };

        // Leer tabla de inodos. Cada registro 128 bytes.
        let inode_record_size = 128usize;
        let mut inodes: Vec<DiskInode> = Vec::with_capacity(total_inodes as usize);

        // calculamos cuántos bloques ocupa la tabla de inodos (ceil)
        let inode_table_bytes = (total_inodes as usize) * inode_record_size;
        let blocks_for_inodes = (inode_table_bytes + (sb.block_size as usize) - 1) / (sb.block_size as usize);

        for bi in 0..blocks_for_inodes {
            let block_idx = (inode_table_start as usize).saturating_add(bi);
            if block_idx >= (total_blocks as usize) { break; }
            let p = folder.join(format!("block_{:04}.png", block_idx));
            let mut fb = File::open(&p).map_err(|e| format!("open inode table block {}: {}", block_idx, e))?;
            let mut block_buf = vec![0u8; sb.block_size as usize];
            fb.read_exact(&mut block_buf).map_err(|e| format!("read inode table block {}: {}", block_idx, e))?;

            let mut off = 0usize;
            while off + inode_record_size <= block_buf.len() && inodes.len() < total_inodes as usize {
                let rec = &block_buf[off..off + inode_record_size];
                let all_zero = rec.iter().all(|&x| x == 0);
                if all_zero {
                    let idx = inodes.len() as u32;
                    inodes.push(DiskInode {
                        inode_number: idx,
                        inode_mode: 0,
                        user_id: 0,
                        group_id: 0,
                        links: 0,
                        size: 0,
                        direct: [0u32; 12],
                        indirect1: 0,
                    });
                } else {
                    let din = DiskInode::from_bytes(rec);
                    inodes.push(din);
                }
                off += inode_record_size;
            }
        }

        // completar con inodos vacíos si hiciera falta
        while inodes.len() < total_inodes as usize {
            let idx = inodes.len() as u32;
            inodes.push(DiskInode {
                inode_number: idx,
                inode_mode: 0,
                user_id: 0,
                group_id: 0,
                links: 0,
                size: 0,
                direct: [0u32; 12],
                indirect1: 0,
            });
        }

        Ok(QRFileSystem {
            folder,
            sb: Arc::new(sb),
            inodes: Arc::new(inodes),
        })
    }

    /// Lee un bloque físico y devuelve exactamente block_size bytes
    fn read_block_bytes(&self, block_index: u32) -> Result<Vec<u8>, String> {
        if block_index as u64 >= self.sb.total_blocks as u64 {
            return Err(format!("block_index fuera de rango {}", block_index));
        }
        let path = self.folder.join(format!("block_{:04}.png", block_index));
        let mut f = File::open(&path).map_err(|e| format!("open block {}: {}", block_index, e))?;
        let mut buf = vec![0u8; self.sb.block_size as usize];
        f.read_exact(&mut buf).map_err(|e| format!("read block {}: {}", block_index, e))?;
        Ok(buf)
    }

    /// Lee todas las entradas de directorio de un bloque (formato u32 + 256 bytes)
    fn read_dir_entries_from_block(&self, block_index: u32) -> Result<Vec<DirEntry>, String> {
        let blk = self.read_block_bytes(block_index)?;
        let mut res = Vec::new();
        let entry_size = 4 + 256;
        let mut off = 0usize;
        while off + entry_size <= blk.len() {
            let inode_id = u32le_from(&blk[off..off + 4]);
            let name_bytes = &blk[off + 4..off + 4 + 256];
            let name = match name_bytes.iter().position(|&b| b == 0) {
                Some(pos) => &name_bytes[..pos],
                None => name_bytes,
            };
            let name = String::from_utf8_lossy(name).to_string();
            // En el mkfs las entradas "." y ".." están escritas; incluimos todas las no vacías.
            if !name.is_empty() {
                res.push(DirEntry { inode: inode_id, name });
            }
            off += entry_size;
        }
        Ok(res)
    }

    /// Construye FileAttr a partir del DiskInode
    fn fileattr_from_inode(&self, inode: &DiskInode) -> FileAttr {
        let perm = (inode.inode_mode & 0o777) as u16;
        let kind = if inode.is_dir() { FileType::Directory } else { FileType::RegularFile };
        let now = SystemTime::now();
        FileAttr {
            ino: inode.inode_number as u64 + 1, // evitamos ino==0
            size: inode.size as u64,
            blocks: ((inode.size as u64 + 511) / 512),
            atime: now,
            mtime: now,
            ctime: now,
            crtime: now,
            kind,
            perm,
            nlink: inode.links as u32,
            uid: inode.user_id,
            gid: inode.group_id,
            rdev: 0,
            flags: 0,
            blksize: 512,
        }
    }

    fn get_inode(&self, inode_number: u32) -> Option<DiskInode> {
        self.inodes.get(inode_number as usize).cloned()
    }

    /// Lee un rango [offset, offset+size) del archivo representado por `inode`.
    /// Implementación que recorre los punteros directos del inodo.
    fn read_file_range(&self, inode: &DiskInode, offset: u64, size: usize) -> Result<Vec<u8>, String> {
        let block_size = self.sb.block_size as u64;
        let file_size = inode.size as u64;
        if offset >= file_size {
            return Ok(vec![]);
        }
        let to_read = std::cmp::min(size as u64, file_size - offset) as usize;

        let mut result = Vec::with_capacity(to_read);
        let start_block_idx = (offset / block_size) as usize;
        let offset_in_first = (offset % block_size) as usize;
        let mut remaining = to_read;

        // iterate over direct pointers from start_block_idx
        for i in start_block_idx..inode.direct.len() {
            let bnum = inode.direct[i];
            if bnum == 0 { break; }
            let block_bytes = self.read_block_bytes(bnum)?;
            if i == start_block_idx {
                let take = std::cmp::min(remaining, (block_size as usize) - offset_in_first);
                result.extend_from_slice(&block_bytes[offset_in_first..offset_in_first + take]);
                remaining -= take;
            } else {
                let take = std::cmp::min(remaining, block_size as usize);
                result.extend_from_slice(&block_bytes[0..take]);
                remaining -= take;
            }
            if remaining == 0 { break; }
        }
        Ok(result)
    }
}

impl Filesystem for QRFileSystem {
    fn init(&mut self, _req: &Request, _config: &mut KernelConfig) -> Result<(), c_int> {
        println!("QRFS: init (version {})", self.sb.version);
        Ok(())
    }

    fn destroy(&mut self) {
        println!("QRFS: destroy");
    }

    fn lookup(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: ReplyEntry) {
        let nm = name.to_string_lossy();
        // map ino -> inode_number: ino = inode_number + 1
        let parent_inode_num = if parent == 1 { self.sb.root_inode } else { (parent - 1) as u32 };

        if let Some(parent_inode) = self.get_inode(parent_inode_num) {
            if !parent_inode.is_dir() {
                reply.error(ENOENT);
                return;
            }
            // asume que el contenido del directorio está en direct[0]
            let dir_block = parent_inode.direct[0];
            match self.read_dir_entries_from_block(dir_block) {
                Ok(entries) => {
                    for e in entries {
                        if e.name == nm {
                            if let Some(target_inode) = self.get_inode(e.inode) {
                                let attr = self.fileattr_from_inode(&target_inode);
                                reply.entry(&Duration::new(1, 0), &attr, 0);
                                return;
                            }
                        }
                    }
                    reply.error(ENOENT);
                }
                Err(_) => reply.error(EIO),
            }
        } else {
            reply.error(ENOENT);
        }
    }

    fn getattr(&mut self, _req: &Request, ino: u64, _size: Option<u64>, reply: ReplyAttr) {
        if ino == 1 {
            if let Some(root_inode) = self.get_inode(self.sb.root_inode) {
                let attr = self.fileattr_from_inode(&root_inode);
                reply.attr(&Duration::new(1, 0), &attr);
                return;
            } else {
                reply.error(ENOENT);
                return;
            }
        }
        let inode_num = (ino - 1) as u32;
        match self.get_inode(inode_num) {
            Some(inode) => {
                let attr = self.fileattr_from_inode(&inode);
                reply.attr(&Duration::new(1, 0), &attr);
            }
            None => reply.error(ENOENT),
        }
    }

    fn open(&mut self, _req: &Request, ino: u64, flags: i32, reply: ReplyOpen) {
        let inode_num = (ino - 1) as u32;
        match self.get_inode(inode_num) {
            Some(inode) => {
                if inode.is_dir() {
                    reply.error(EISDIR);
                } else {
                    // no manejamos fh especiales, retornamos fh=0
                    reply.opened(0, 0);
                }
            }
            None => reply.error(ENOENT),
        }
    }

    fn read(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        size: u32,
        _flags: i32,
        _lock_owner: Option<u64>,
        reply: ReplyData,
    ) {
        let inode_num = (ino - 1) as u32;
        match self.get_inode(inode_num) {
            Some(inode) => {
                if inode.is_dir() {
                    reply.error(EIO);
                    return;
                }
                let off = offset as u64;
                match self.read_file_range(&inode, off, size as usize) {
                    Ok(data) => reply.data(&data),
                    Err(_) => reply.error(EIO),
                }
            }
            None => reply.error(ENOENT),
        }
    }

    fn readdir(&mut self, _req: &Request, ino: u64, _fh: u64, offset: i64, mut reply: ReplyDirectory) {
        if ino != 1 {
            reply.error(ENOENT);
            return;
        }
        let root_inode = match self.get_inode(self.sb.root_inode) {
            Some(i) => i,
            None => {
                reply.error(ENOENT);
                return;
            }
        };
        let dir_block = root_inode.direct[0];
        let entries = match self.read_dir_entries_from_block(dir_block) {
            Ok(v) => v,
            Err(_) => {
                reply.error(EIO);
                return;
            }
        };

        // fuser expects '.' and '..' manually
        if offset == 0 {
            let _ = reply.add(1, 1, FileType::Directory, ".");
            let _ = reply.add(1, 2, FileType::Directory, "..");
        }

        // offset is number of entries already read (we'll interpret as index into entries)
        let start_idx = if offset <= 0 { 0usize } else { offset as usize - 1 };
        for (i, e) in entries.iter().enumerate().skip(start_idx) {
            let ino = (e.inode as u64) + 1;
            let kind = match self.get_inode(e.inode) {
                Some(ref inode) => {
                    if inode.is_dir() { FileType::Directory } else { FileType::RegularFile }
                }
                None => FileType::RegularFile,
            };
            let next_offset = (i as i64) + 1;
            if reply.add(ino, next_offset, kind, &e.name) {
                break;
            }
        }
        reply.ok();
    }
}

use std::env;
use std::fs::{self, OpenOptions};
use std::process;

type U32t = u32;

/// Escribe un u32 en little-endian en el slice (length >= 4).
fn u32le_write(v: U32t, p: &mut [u8]) {
    let b = v.to_le_bytes();
    p[0..4].copy_from_slice(&b);
}

/// División con techo para u32
fn ceil_div(a: U32t, b: U32t) -> U32t {
    (a + b - 1) / b
}

fn ensure_folder(folder: &str) -> Result<(), String> {
    fs::create_dir_all(folder).map_err(|e| format!("mkdir -p {} : {}", folder, e))
}

fn create_zero_block(folder: &str, index: U32t, block_size: usize) -> Result<(), String> {
    let path = format!("{}/block_{:04}.png", folder, index);
    let mut f = File::create(&path).map_err(|e| format!("create {}: {}", path, e))?;
    let zeros = vec![0u8; block_size];
    f.write_all(&zeros).map_err(|e| format!("write zeros {}: {}", path, e))?;
    Ok(())
}

fn write_block(folder: &str, index: U32t, buf: &[u8], len: usize) -> Result<(), String> {
    let path = format!("{}/block_{:04}.png", folder, index);
    // Abrir para read+write; asumimos que el archivo ya existe (creado por create_zero_block)
    let mut f = OpenOptions::new()
        .write(true)
        .read(true)
        .open(&path)
        .map_err(|e| format!("open {}: {}", path, e))?;
    f.seek(SeekFrom::Start(0))
        .map_err(|e| format!("seek {}: {}", path, e))?;
    f.write_all(&buf[0..len])
        .map_err(|e| format!("write {}: {}", path, e))?;
    Ok(())
}

/// Escribe el superbloque con offsets en el bloque 0
fn write_superblock_with_offsets(
    folder: &str,
    block_size: U32t,
    total_blocks: U32t,
    total_inodes: U32t,
    inode_bitmap_128: &[u8; 128],
    data_bitmap_128: &[u8; 128],
    root_inode: U32t,
    inode_bitmap_start: U32t,
    inode_bitmap_blocks: U32t,
    data_bitmap_start: U32t,
    data_bitmap_blocks: U32t,
    inode_table_start: U32t,
    inode_table_blocks: U32t,
    data_region_start: U32t,
) -> Result<(), String> {
    let mut buf = vec![0u8; block_size as usize];

    // Magic
    buf[0] = b'Q';
    buf[1] = b'R';
    buf[2] = b'F';
    buf[3] = b'S';

    u32le_write(1, &mut buf[4..8]); // version
    u32le_write(block_size, &mut buf[8..12]);
    u32le_write(total_blocks, &mut buf[12..16]);
    u32le_write(total_inodes, &mut buf[16..20]);

    // bitmaps (128 bytes cada uno)
    buf[20..20 + 128].copy_from_slice(&inode_bitmap_128[..]);
    buf[148..148 + 128].copy_from_slice(&data_bitmap_128[..]);

    u32le_write(root_inode, &mut buf[276..280]);

    // offsets y tamaños
    u32le_write(inode_bitmap_start, &mut buf[280..284]);
    u32le_write(inode_bitmap_blocks, &mut buf[284..288]);
    u32le_write(data_bitmap_start, &mut buf[288..292]);
    u32le_write(data_bitmap_blocks, &mut buf[292..296]);
    u32le_write(inode_table_start, &mut buf[296..300]);
    u32le_write(inode_table_blocks, &mut buf[300..304]);
    u32le_write(data_region_start, &mut buf[304..308]);

    write_block(folder, 0, &buf, buf.len())
}

/// Serializa un inodo en 128 bytes (formato C original)
fn inode_serialize128(
    out: &mut [u8; 128],
    inode_number: U32t,
    inode_mode: U32t,
    user_id: U32t,
    group_id: U32t,
    links: U32t,
    size: U32t,
    direct: &[U32t; 12],
    indirect1: U32t,
) {
    for b in out.iter_mut() {
        *b = 0;
    }
    u32le_write(inode_number, &mut out[0..4]);
    u32le_write(inode_mode, &mut out[4..8]);
    u32le_write(user_id, &mut out[8..12]);
    u32le_write(group_id, &mut out[12..16]);
    u32le_write(links, &mut out[16..20]);
    u32le_write(size, &mut out[20..24]);
    for i in 0..12 {
        u32le_write(direct[i], &mut out[24 + i * 4..24 + i * 4 + 4]);
    }
    u32le_write(indirect1, &mut out[72..76]);
}

/// Construye un bloque de directorio raíz con "." y ".."
fn build_root_dir_block(block: &mut [u8], block_size: usize, root_inode: U32t) {
    for b in block.iter_mut().take(block_size) {
        *b = 0;
    }
    // entrada ".": inode (u32) + nombre (256 bytes)
    u32le_write(root_inode, &mut block[0..4]);
    let dot = b".";
    let name_slice = &mut block[4..4 + 256];
    name_slice[..dot.len()].copy_from_slice(dot);

    // entrada ".."
    u32le_write(root_inode, &mut block[264..268]);
    let dotdot = b"..";
    let name_slice2 = &mut block[268..268 + 256];
    name_slice2[..dotdot.len()].copy_from_slice(dotdot);
}

fn usage_and_exit(prog: &str) -> ! {
    eprintln!("Usage: {} [--blocks=N] [--inodes=M] [--blocksize=B] <folder>", prog);
    process::exit(2);
}

fn main() {
    // defaults
    let mut block_size: U32t = 1024;
    let mut total_blocks: U32t = 100;
    let mut total_inodes: U32t = 10;

    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        usage_and_exit(&args[0]);
    }

    // parse simples: flags --blocks=, --inodes=, --blocksize=
    let mut folder_arg = None;
    for a in args.iter().skip(1) {
        if let Some(rest) = a.strip_prefix("--blocks=") {
            match rest.parse::<U32t>() {
                Ok(v) => total_blocks = v,
                Err(_) => {
                    eprintln!("Bad --blocks value");
                    usage_and_exit(&args[0]);
                }
            }
        } else if let Some(rest) = a.strip_prefix("--inodes=") {
            match rest.parse::<U32t>() {
                Ok(v) => total_inodes = v,
                Err(_) => {
                    eprintln!("Bad --inodes value");
                    usage_and_exit(&args[0]);
                }
            }
        } else if let Some(rest) = a.strip_prefix("--blocksize=") {
            match rest.parse::<U32t>() {
                Ok(v) => block_size = v,
                Err(_) => {
                    eprintln!("Bad --blocksize value");
                    usage_and_exit(&args[0]);
                }
            }
        } else if folder_arg.is_none() {
            folder_arg = Some(a.clone());
        } else {
            usage_and_exit(&args[0]);
        }
    }

    let folder = folder_arg.unwrap_or_else(|| "qrfolder".to_string());

    // validations
    if total_blocks > 128 || total_inodes > 128 {
        eprintln!("Por ahora mkfs.qrfs soporta como máximo 128 bloques e inodos (para bitmaps en 1 bloque).");
        process::exit(2);
    }
    if block_size < 512 || block_size > 65536 {
        eprintln!("block_size fuera de rango razonable (512..65536).");
        process::exit(2);
    }

    // 1) crear carpeta y bloques cero
    if let Err(e) = ensure_folder(&folder) {
        eprintln!("No se pudo preparar la carpeta destino: {}", e);
        process::exit(1);
    }

    for i in 0..total_blocks {
        if let Err(e) = create_zero_block(&folder, i, block_size as usize) {
            eprintln!("No se pudo crear block_{:04}.png: {}", i, e);
            process::exit(1);
        }
    }

    // 2) offsets (mismo esquema que el C)
    let inode_bitmap_start: U32t = 1;
    let inode_bitmap_blocks: U32t = 1;
    let data_bitmap_start: U32t = inode_bitmap_start + inode_bitmap_blocks;
    let data_bitmap_blocks: U32t = 1;
    let inode_table_start: U32t = data_bitmap_start + data_bitmap_blocks;

    let inode_record_size: U32t = 128;
    let inode_table_bytes: U32t = total_inodes * inode_record_size;
    let inode_table_blocks: U32t = ceil_div(inode_table_bytes, block_size);

    let data_region_start: U32t = inode_table_start + inode_table_blocks;

    if data_region_start >= total_blocks {
        eprintln!("No hay espacio para región de datos (data_region_start={}, total_blocks={}).", data_region_start, total_blocks);
        process::exit(1);
    }

    // 3) construir bitmaps en RAM (llenados con '0'/'1')
    let mut inode_bitmap = [b'0'; 128];
    let mut data_bitmap = [b'0'; 128];

    // inodo raíz
    let root_inode: U32t = 0;
    inode_bitmap[root_inode as usize] = b'1';

    // bloques reservados
    data_bitmap[0] = b'1'; // superbloque
    for b in inode_bitmap_start..(inode_bitmap_start + inode_bitmap_blocks) {
        if (b as usize) < 128 { data_bitmap[b as usize] = b'1'; }
    }
    for b in data_bitmap_start..(data_bitmap_start + data_bitmap_blocks) {
        if (b as usize) < 128 { data_bitmap[b as usize] = b'1'; }
    }
    for b in inode_table_start..(inode_table_start + inode_table_blocks) {
        if (b as usize) < 128 { data_bitmap[b as usize] = b'1'; }
    }

    // asignar 1 bloque para directorio raíz (primer bloque de data region)
    let root_dir_block = data_region_start;
    if (root_dir_block as usize) < 128 {
        data_bitmap[root_dir_block as usize] = b'1';
    } else {
        eprintln!("root_dir_block fuera de rango del bitmap");
        process::exit(1);
    }

    // 4) escribir bitmaps en sus bloques (escribimos block_size bytes; los 128 bytes están al inicio)
    {
        let mut buf = vec![0u8; block_size as usize];
        buf[..128].copy_from_slice(&inode_bitmap);
        if let Err(e) = write_block(&folder, inode_bitmap_start, &buf, buf.len()) {
            eprintln!("Error escribiendo bitmap de inodos: {}", e);
            process::exit(1);
        }
    }
    {
        let mut buf = vec![0u8; block_size as usize];
        buf[..128].copy_from_slice(&data_bitmap);
        if let Err(e) = write_block(&folder, data_bitmap_start, &buf, buf.len()) {
            eprintln!("Error escribiendo bitmap de bloques: {}", e);
            process::exit(1);
        }
    }

    // 5) escribir tabla de inodos con el inodo raíz
    // construir registro 0 (raíz)
    let mut rec = [0u8; 128];
    let mut direct = [0u32; 12];
    for i in 0..12 { direct[i] = 0; }
    direct[0] = root_dir_block;

    // modo directorio 0040000 | 0755
    let mode_dir: U32t = 0o040000u32 | 0o755u32;
    let dir_size: U32t = 520; // 2 entradas * 260 bytes

    inode_serialize128(&mut rec, root_inode, mode_dir, 0, 0, 2, dir_size, &direct, 0);

    // escribir el primer bloque de la tabla de inodos (offset 0 dentro del bloque)
    {
        let mut itbl_block0 = vec![0u8; block_size as usize];
        itbl_block0[..128].copy_from_slice(&rec);
        if let Err(e) = write_block(&folder, inode_table_start, &itbl_block0, itbl_block0.len()) {
            eprintln!("Error escribiendo tabla de inodos (bloque 0): {}", e);
            process::exit(1);
        }
    }

    // 6) escribir bloque del directorio raíz
    {
        let mut dirblk = vec![0u8; block_size as usize];
        build_root_dir_block(&mut dirblk, block_size as usize, root_inode);
        if let Err(e) = write_block(&folder, root_dir_block, &dirblk, dirblk.len()) {
            eprintln!("Error escribiendo directorio raíz: {}", e);
            process::exit(1);
        }
    }

    // 7) escribir superbloque con offsets
    if let Err(e) = write_superblock_with_offsets(
        &folder,
        block_size,
        total_blocks,
        total_inodes,
        &inode_bitmap,
        &data_bitmap,
        root_inode,
        inode_bitmap_start,
        inode_bitmap_blocks,
        data_bitmap_start,
        data_bitmap_blocks,
        inode_table_start,
        inode_table_blocks,
        data_region_start,
    ) {
        eprintln!("Error escribiendo superbloque: {}", e);
        process::exit(1);
    }

    // 8) reporte
    println!("QRFS creado en '{}'", folder);
    println!("block_size={}, total_blocks={}, total_inodes={}", block_size, total_blocks, total_inodes);
    println!("Layout:");
    println!("  SB               : block 0");
    println!("  inode_bitmap     : start={}, blocks={}", inode_bitmap_start, inode_bitmap_blocks);
    println!("  data_bitmap      : start={}, blocks={}", data_bitmap_start, data_bitmap_blocks);
    println!("  inode_table      : start={}, blocks={} (record_size=128)", inode_table_start, inode_table_blocks);
    println!("  data_region_start: {}", data_region_start);
    println!("  root inode       : {}  (direct[0]={}, size={})", root_inode, root_dir_block, dir_size);
}
