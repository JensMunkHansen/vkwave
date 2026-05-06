import qrenderdoc as qrd
import renderdoc as rd

_TITLE = "Debug compute thread for picked pixel"


def register(version, ctx):
    ctx.Extensions().RegisterWindowMenu(
        qrd.WindowMenu.Tools,
        [_TITLE],
        _on_menu,
    )


def unregister():
    pass


def _on_menu(ctx, data):
    tv = ctx.GetTextureViewer()
    if tv is None:
        ctx.Extensions().MessageDialog("Texture viewer not open.", _TITLE)
        return

    px, py = tv.GetPickedLocation()
    if px < 0 or py < 0:
        ctx.Extensions().MessageDialog(
            "No pixel picked. Right-click a pixel in the Texture Viewer first.",
            _TITLE,
        )
        return

    # Slice / array layer for 3D textures and 2D arrays.
    pz = 0
    try:
        disp = tv.GetTextureDisplay()
        if disp is not None:
            pz = int(disp.subresource.slice)
    except Exception:
        pass

    pipe = ctx.CurPipelineState()
    cs_ref = pipe.GetShaderReflection(rd.ShaderStage.Compute)
    if cs_ref is None:
        ctx.Extensions().MessageDialog(
            "No compute shader bound at the current event. Select a Dispatch in the Event Browser.",
            _TITLE,
        )
        return

    lx, ly, lz = cs_ref.dispatchThreadsDimension
    if lx == 0 or ly == 0 or lz == 0:
        ctx.Extensions().MessageDialog(
            f"Compute shader local_size has a zero component: ({lx},{ly},{lz}).",
            _TITLE,
        )
        return

    gx, tx = divmod(int(px), lx)
    gy, ty = divmod(int(py), ly)
    gz, tz = divmod(int(pz), lz)

    pipe_id = pipe.GetComputePipelineObject()
    qt = ctx.Extensions().GetMiniQtHelper()

    def on_replay(replay):
        trace = replay.DebugThread((gx, gy, gz), (tx, ty, tz))

        if trace is None or trace.debugger is None:
            msg = (
                f"DebugThread produced no trace.\n\n"
                f"pixel = ({px}, {py}, {pz})\n"
                f"local_size = ({lx}, {ly}, {lz})\n"
                f"group = ({gx}, {gy}, {gz})\n"
                f"thread = ({tx}, {ty}, {tz})"
            )
            qt.InvokeOntoUIThread(lambda: ctx.Extensions().MessageDialog(msg, _TITLE))
            return

        def on_ui():
            label = (
                f"Pixel ({px},{py},{pz})  ->  "
                f"group ({gx},{gy},{gz}) thread ({tx},{ty},{tz})"
            )
            viewer = ctx.DebugShader(cs_ref, pipe_id, trace, label)
            if viewer is not None:
                ctx.AddDockWindow(viewer.Widget(), qrd.DockReference.MainToolArea, None)

        qt.InvokeOntoUIThread(on_ui)

    ctx.Replay().AsyncInvoke("vkwave-compute-pixel-debug", on_replay)
