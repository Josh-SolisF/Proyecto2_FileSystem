mod filesystem;

use std::env;
use std::path::PathBuf;

use fuser::MountOption;
use filesystem::QRFileSystem;

fn usage_and_exit(program: &str) -> ! {
    eprintln!("Uso: {} <carpeta_qrfs> <punto_de_montaje>", program);
    std::process::exit(2);
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        usage_and_exit(&args[0]);
    }

    let folder = PathBuf::from(&args[1]);
    let mountpoint = PathBuf::from(&args[2]);

    if !folder.is_dir() {
        eprintln!(
            "La carpeta '{}' no existe o no contiene un QRFS válido.\n\
             Debes crearla primero ejecutando:\n    mkfs_qrfs {}",
            folder.display(),
            folder.display()
        );
        std::process::exit(1);
    }

    let qrfs = match QRFileSystem::from_folder(folder.clone()) {
        Ok(fs) => fs,
        Err(e) => {
            eprintln!("Error inicializando QRFS desde '{}': {}", folder.display(), e);
            std::process::exit(1);
        }
    };

    let mount_options = vec![
        MountOption::RO,
        MountOption::FSName("qr_file_system".parse().unwrap()),
    ];

    println!("Montando QRFS desde '{}' en '{}'", folder.display(), mountpoint.display());
    if let Err(e) = fuser::mount2(qrfs, mountpoint, &mount_options) {
        eprintln!("Error montando: {}", e);
        std::process::exit(1);
    }
}
