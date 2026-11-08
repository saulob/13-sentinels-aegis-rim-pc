"""Per-title facts for the game this port knows, keyed by TITLE_ID in sce_sys/param.sfo.

A version's layers are its packages, the base first and then the updates applied over it; the last layer's param.sfo is
the finished game's. launcher/game_profile.cpp mirrors these values, and tests/game_profile_tests.cpp checks that they agree.
"""
from pathlib import Path

from ps4_module import parse_sfo

BASE_LAYER = {'app_ver': '01.00', 'sfo_version': '01.00', 'update': False}
BASE_LAYER_0101 = {'app_ver': '01.00', 'sfo_version': '01.01', 'update': False}
LANGUAGE_ARCHIVES = ('ROBO_FR.CPK', 'ROBO_GE.CPK', 'ROBO_IT.CPK', 'ROBO_SP.CPK')
# Linked after the eboot in this order: libc.prx needs libSceFios2.prx.
MODULES = ('libc.prx', 'libSceFios2.prx', 'libSceNpToolkit2.prx')
# What the loader needs; the game data (ROBO*.CPK) is only unpacked with --scope full.
CORE_PATTERNS = ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt')

PROFILES = {
    'CUSA19610': {
        'name': '13 Sentinels: Aegis Rim',
        'region': 'USA',
        'content_id': 'UP0177-CUSA19610_00-13SENTINELSEFIGS',
        'versions': {
            '1.00': {
                'layers': (BASE_LAYER,),
                'eboot_sha256': 'cf7e90939842b3c9c02f9116e26c2e398610312217fea05d52a1595c60ed5950',
                'folder': 'CUSA19610-1.00', 'regional_archive': 'ROBO_US.CPK', 'extra_files': LANGUAGE_ARCHIVES,
            },
            # Each update is laid over the base package alone; its eboot also opens the ROBO_PATCH archives it ships.
            '1.01': {
                'layers': (BASE_LAYER, {'app_ver': '01.01', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': 'dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533',
                'folder': 'CUSA19610-1.01', 'regional_archive': 'ROBO_US.CPK',
                'extra_files': LANGUAGE_ARCHIVES + ('ROBO_PATCH_US.CPK', 'sce_discmap_patch.plt'),
            },
            '1.02': {
                'layers': (BASE_LAYER, {'app_ver': '01.02', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': '075250099ba56c2e8f35b4dc8d35b25c5f98304e45c8546c790d56fe87803ff1',
                'folder': 'CUSA19610-1.02', 'regional_archive': 'ROBO_US.CPK',
                'extra_files': LANGUAGE_ARCHIVES + (
                    'ROBO_PATCH_US.CPK', 'ROBO_PATCH2_US.CPK', 'ROBO_PATCH2_FR.CPK', 'ROBO_PATCH2_GE.CPK',
                    'ROBO_PATCH2_IT.CPK', 'ROBO_PATCH2_SP.CPK', 'sce_discmap_patch.plt'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': MODULES,
        'core_patterns': CORE_PATTERNS,
    },
    'CUSA19620': {
        'name': '13 Sentinels: Aegis Rim',
        'region': 'Europe',
        'content_id': 'EP0177-CUSA19620_00-13SENTINELSEFIGS',
        'versions': {
            '1.00': {
                'layers': (BASE_LAYER_0101,),
                'eboot_sha256': 'e4df1d07de8afac5f55c91ea5f54f86f8b3c9999ca7e2ac3d602d04ea11490e1',
                'folder': 'CUSA19620-1.00', 'regional_archive': 'ROBO_US.CPK', 'extra_files': LANGUAGE_ARCHIVES,
            },
            '1.01': {
                'layers': (BASE_LAYER_0101, {'app_ver': '01.01', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': 'dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533',
                # The same update repackaged for older firmware: only the eboot's SDK version fields differ.
                'alternate_eboots': ('123730cd2ee9b1ebbed4a6cc2fe7854154aad82645ef12da15d7e34b82da6b28',),
                'folder': 'CUSA19620-1.01', 'regional_archive': 'ROBO_US.CPK',
                'extra_files': LANGUAGE_ARCHIVES + ('ROBO_PATCH_US.CPK', 'sce_discmap_patch.plt'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': MODULES,
        'core_patterns': CORE_PATTERNS,
    },
    'CUSA10602': {
        'name': '13 Sentinels: Aegis Rim',
        'region': 'Japan',
        'content_id': 'JP0005-CUSA10602_00-13K0000000000000',
        'versions': {
            '1.00': {
                'layers': (BASE_LAYER,),
                'eboot_sha256': '6d6f67ce332091875c56d211a60d1158075e6516afc0b188029ed420dee85d0c',
                'folder': 'CUSA10602-1.00', 'regional_archive': 'ROBO.CPK', 'extra_files': (),
            },
            '1.03': {
                'layers': (BASE_LAYER, {'app_ver': '01.03', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': 'fdd1b5ad5d8a42caf81397f19dc53914bcc927fe06fd077845d425528e07c11a',
                'folder': 'CUSA10602-1.03', 'regional_archive': 'ROBO.CPK',
                'extra_files': ('ROBO_PATCH.CPK', 'ROBO_PATCH2.CPK', 'sce_discmap_patch.plt'),
            },
            # 1.04 holds everything of 1.03 and is laid over the base alone.
            '1.04': {
                'layers': (BASE_LAYER, {'app_ver': '01.04', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': '3bcaeac50607e4ded31b5123a4170f646bdb0441a070fe9ce6757dfd60c4fbcb',
                'folder': 'CUSA10602-1.04', 'regional_archive': 'ROBO.CPK',
                'extra_files': ('ROBO_PATCH.CPK', 'sce_discmap_patch.plt'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': MODULES,
        'core_patterns': CORE_PATTERNS,
    },
    'CUSA18093': {
        'name': '13 Sentinels: Aegis Rim',
        'region': 'Asia',
        'content_id': 'HP0177-CUSA18093_00-13K0000000000000',
        'versions': {
            '1.00': {
                'layers': (BASE_LAYER_0101,),
                'eboot_sha256': 'dd6d54e99b1246dbecd1fa9556bf90e58fe6ff5cfdf01f010bfd9a4c6c572bcf',
                'folder': 'CUSA18093-1.00', 'regional_archive': 'ROBO_CN.CPK', 'extra_files': ('ROBO_TW.CPK',),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': MODULES,
        'core_patterns': CORE_PATTERNS,
    },
}


def accepts_eboot(build, sha):
    """True when `sha` is the build's eboot.bin or an alternate packaging of it."""
    return sha == build['eboot_sha256'] or sha in build.get('alternate_eboots', ())


def title_id(game):
    """TITLE_ID of a game folder, or None."""
    try:
        return parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes()).get('TITLE_ID')
    except (OSError, ValueError, KeyError):
        return None


def profile(game_or_title):
    """The profile for a TITLE_ID or a game folder, or None for an unknown title."""
    key = str(game_or_title)
    if key not in PROFILES:
        key = title_id(game_or_title) or ''
    return PROFILES.get(key)


def finished_layer(build):
    """The layer whose param.sfo the installed game has."""
    return build['layers'][-1]


def layer_identity(title, layer):
    """(TITLE_ID, APP_VER, VERSION, is an update) of the package a layer is."""
    return title, layer['app_ver'], layer['sfo_version'], layer['update']


def package_identity(info):
    """The identity of a package, as layer_identity() names it, or None when it is not a package of a supported title."""
    sfo = info['param_sfo']
    found = PROFILES.get(sfo.get('TITLE_ID', ''))
    if not found or sfo.get('CONTENT_ID') != found['content_id']:
        return None
    return sfo['TITLE_ID'], sfo.get('APP_VER'), sfo.get('VERSION'), sfo.get('CATEGORY') == 'gp'


def finishing_version(title, identity):
    """The version of `title` whose finished game is the package with this identity, or None."""
    for version, build in PROFILES[title]['versions'].items():
        if layer_identity(title, finished_layer(build)) == identity:
            return version
    return None


def version_of(game):
    """The profile version whose finished param.sfo matches the game folder, or None."""
    try:
        sfo = parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes())
    except (OSError, ValueError):
        return None
    found = PROFILES.get(sfo.get('TITLE_ID', ''))
    if not found:
        return None
    return next((v for v, build in found['versions'].items()
                 if (finished_layer(build)['app_ver'], finished_layer(build)['sfo_version'])
                 == (sfo.get('APP_VER'), sfo.get('VERSION'))), None)


def package_layer(info):
    """(TITLE_ID, profile version, layer index) of the package of a supported build's finished game, or None.

    `info` is what scripts/pkg_info.py reads. The index is the package's place in the build's layers.
    """
    identity = package_identity(info)
    if identity is None:
        return None
    version = finishing_version(identity[0], identity)
    if version is None:
        return None
    return identity[0], version, len(PROFILES[identity[0]]['versions'][version]['layers']) - 1
