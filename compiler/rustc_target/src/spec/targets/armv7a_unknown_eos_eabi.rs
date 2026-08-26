use crate::spec::{
    Arch, Cc, CfgAbi, FloatAbi, FramePointer, LinkerFlavor, Lld, Os, PanicStrategy,
    RelocModel, Target, TargetMetadata, TargetOptions, cvs,
};

pub(crate) fn target() -> Target {
    Target {
        llvm_target: "armv7a-unknown-none-eabi".into(),
        metadata: TargetMetadata {
            description: Some("ARMv7-A Cortex-A9 with EOS".into()),
            tier: Some(3),
            host_tools: Some(false),
            std: Some(true),
        },
        pointer_width: 32,
        data_layout: "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64".into(),
        arch: Arch::Arm,
        options: TargetOptions {
            os: Os::Eos,
            families: cvs!["unix"],
            cfg_abi: CfgAbi::Eabi,
            linker: Some("eos-rust-link".into()),
            linker_flavor: LinkerFlavor::Gnu(Cc::Yes, Lld::No),
            cpu: "cortex-a9".into(),
            features: "+v7,+thumb2,+vfp3,+neon,+strict-align".into(),
            llvm_floatabi: Some(FloatAbi::Soft),
            dynamic_linking: true,
            position_independent_executables: true,
            relocation_model: RelocModel::Pic,
            panic_strategy: PanicStrategy::Unwind,
            default_uwtable: true,
            frame_pointer: FramePointer::Always,
            max_atomic_width: Some(64),
            c_enum_min_bits: Some(8),
            has_thread_local: false,
            has_thumb_interworking: true,
            emit_debug_gdb_scripts: false,
            ..Default::default()
        },
    }
}
