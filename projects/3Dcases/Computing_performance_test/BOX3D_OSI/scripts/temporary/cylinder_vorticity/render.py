# 本次圆柱涡量展示的复现入口，保留以复现图片。
from paraview.simple import *
from paraview import servermanager
from pathlib import Path
import json, socket
root=Path('/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI')
source=root/'runs/box_cylinder_native_520000/production/plt_584000'
out=root/'runs/box_cylinder_native_520000/production/plots/vorticity_584000'
out.mkdir(parents=True,exist_ok=True)
r=AMReXBoxLibGridReader(FileNames=[str(source)])
print('PROPERTIES',r.ListProperties(),flush=True)
r.CellArrayStatus=['ux','uy']
r.Level=3
r.UpdatePipeline()
vel=Calculator(Input=r); vel.AttributeType='Cell Data'; vel.ResultArrayName='velocity'; vel.Function='ux*iHat+uy*jHat'
grad=Gradient(Input=vel); grad.ScalarArray=['CELLS','velocity']; grad.ComputeVorticity=1; grad.VorticityArrayName='curl'; grad.ComputeGradient=0
signed=Calculator(Input=grad); signed.AttributeType='Cell Data'; signed.ResultArrayName='omega_z'; signed.Function='curl_Z'; signed.UpdatePipeline()
print('SIGNED RANGE',signed.CellData['omega_z'].GetRange(),flush=True)
v=CreateView('RenderView'); v.ViewSize=[1800,1000]; v.Background=[1,1,1]; v.UseColorPaletteForBackground=0
v.OrientationAxesVisibility=0; v.CameraParallelProjection=1
v.CameraPosition=[.5,.5,5]; v.CameraFocalPoint=[.5,.5,0]; v.CameraViewUp=[0,1,0]; v.CameraParallelScale=.55
s=Show(signed,v); s.Representation='Surface'; s.Ambient=1; s.Diffuse=0; ColorBy(s,('CELLS','omega_z'))
lut=GetColorTransferFunction('omega_z'); lut.ApplyPreset('Cool to Warm (Extended)',True); lut.RescaleTransferFunction(-3,3); lut.ColorSpace='RGB'; lut.RGBPoints=[-3,.1,.25,.85,0,1,1,1,3,.85,.1,.1]
s.SetScalarBarVisibility(v,True)
bar=GetScalarBar(lut,v); bar.Title='omega_z (1/time)'; bar.ComponentTitle=''; bar.WindowLocation='Lower Center'; bar.Orientation='Horizontal'; bar.ScalarBarLength=.45
bar.TitleColor=[0,0,0]; bar.LabelColor=[0,0,0]
# 使用每个 AMR block 的外框，避免画出所有单元边线。
o=Outline(Input=r); o.UpdatePipeline(); od=Show(o,v); od.DiffuseColor=[.15,.5,.2]; od.LineWidth=.5; od.Opacity=.25
c=Disk(); c.InnerRadius=0; c.OuterRadius=.0125; c.CircumferentialResolution=96
tr=Transform(Input=c); tr.Transform.Translate=[.5,.5,.0001]
cd=Show(tr,v); cd.DiffuseColor=[.2,.2,.2]
t=Text(); t.Text='BOX3D_OSI | step 584000 | Re = 40 | D = 0.025'
td=Show(t,v); td.Color=[0,0,0]; td.FontSize=16; td.WindowLocation='Upper Center'
Render(v); SaveScreenshot(str(out/'vorticity_amr.png'),v,ImageResolution=[1800,1000])
SaveState(str(out/'vorticity_amr.pvsm'))
Hide(o,v)
SaveScreenshot(str(out/'vorticity_clean.png'),v,ImageResolution=[1800,1000])
(out/'metadata.json').write_text(json.dumps({'input':str(source),'step':584000,'host':socket.gethostname(),'bounds':list(r.GetDataInformation().GetBounds()),'vorticity_range':list(signed.CellData['omega_z'].GetRange()),'color_range':[-3,3],'normalization':'signed curl_z of velocity, per-AMR-block derivative; block-edge local differences; no physical rescaling','reference':'none','max_level':3},indent=2))
