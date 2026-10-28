"""桥协议的动作与字段声明；供投递器和单文件客户端共用，不加载 ParaView。"""

DEFAULT_BRIDGE_ROOT = '/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge'

# 经桥投递的动作及其命令行选项：required 缺失即报错，defaults 总是携带，
# optional 仅在给出时携带。键名同时是 argparse 属性名与命令 JSON 字段名。
# 客户端能力由本表派生，运行层的实际分发仍由回归检查。
BRIDGE_ACTIONS = {
    'inspect': {},
    'background': {},
    'restore_background': {},
    'export_centerline': {'required': ('input', 'output'),
                          'defaults': ('resolution', 'max_level')},
    'add_vorticity_view': {'required': ('source',), 'defaults': ('slice_axis', 'component'),
                           'optional': ('color_range', 'slice_position')},
    'repair_vorticity_slice': {'required': ('source',)},
    'publication_vorticity': {'required': ('source', 'color_range'),
                              'defaults': ('vorticity_scale', 'style'),
                              'optional': ('image_size', 'view_id', 'output')},
    'add_axis_title_view': {'required': ('source',), 'optional': ('reset_view', 'output')},
    'align_publication': {'required': ('source',), 'optional': ('view_id', 'output')},
}
# reload_client 由 run_client_status 专用流程投递，只携带期望哈希。
RELOAD_OPTIONS = ('expected_sha256',)
BRIDGE_OPTION_KEYS = frozenset(key for spec in BRIDGE_ACTIONS.values()
                               for group in spec.values() for key in group) | set(RELOAD_OPTIONS)


CAPABILITIES = [*BRIDGE_ACTIONS, 'reload_client']
