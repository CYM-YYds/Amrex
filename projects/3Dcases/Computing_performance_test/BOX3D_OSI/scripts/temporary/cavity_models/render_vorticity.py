# 本任务临时ParaView出图脚本；保留作图像复现材料，任务废弃后可删除。
from pathlib import Path
import hashlib,json,math,socket
from paraview import simple as pvs
from vtkmodules.util.numpy_support import vtk_to_numpy
from vtkmodules.vtkCommonCore import vtkPoints,vtkIntArray
from vtkmodules.vtkCommonDataModel import vtkPolyData,vtkCellArray
from vtkmodules.vtkIOXML import vtkXMLPolyDataWriter
import numpy as np
CASE=Path(__file__).resolve().parents[3]
RUN=CASE/'runs/cavity_models_t1000/job626034'
SOURCE=RUN/'D2Q9_amr_t1000/plt_vort_512000'
OUT=RUN/'figures/vorticity_amr';OUT.mkdir(parents=True,exist_ok=True)
pvs._DisableFirstRenderCameraReset()
reader=pvs.AMReXBoxLibGridReader(registrationName='D2Q9_native_vorticity_t1000',FileNames=[str(SOURCE)])
reader.Level=2;reader.CellArrayStatus=['Vorticity'];reader.UpdatePipeline()
data=reader.GetClientSideObject().GetOutputDataObject(0)
assert data.IsA('vtkOverlappingAMR') and data.GetNumberOfLevels()==3,data.GetClassName()
records=[];ranges=[]
for lev in range(3):
    points=vtkPoints();lines=vtkCellArray();field=vtkIntArray();field.SetName('AMRLevel');field.InsertNextValue(lev)
    blocks=[]
    for index in range(data.GetNumberOfDataSets(lev)):
        block=data.GetDataSet(lev,index)
        if block is None:continue
        bounds=block.GetBounds();assert bounds[0]>=0 and bounds[1]<=1 and bounds[2]>=0 and bounds[3]<=1
        values=vtk_to_numpy(block.GetCellData().GetArray('Vorticity'));assert np.isfinite(values).all()
        ranges.append((float(values.min()),float(values.max())))
        blocks.append(dict(index=index,bounds=list(bounds),cells=block.GetNumberOfCells()))
        corners=[(bounds[0],bounds[2],0.0001),(bounds[1],bounds[2],0.0001),(bounds[1],bounds[3],0.0001),(bounds[0],bounds[3],0.0001)]
        first=points.GetNumberOfPoints()
        for point in corners:points.InsertNextPoint(*point)
        for a,b in [(0,1),(1,2),(2,3),(3,0)]:lines.InsertNextCell(2);lines.InsertCellPoint(first+a);lines.InsertCellPoint(first+b)
    poly=vtkPolyData();poly.SetPoints(points);poly.SetLines(lines);poly.GetFieldData().AddArray(field)
    writer=vtkXMLPolyDataWriter();writer.SetFileName(str(OUT/f'level{lev}_boxes.vtp'));writer.SetInputData(poly);assert writer.Write()==1
    records.append(dict(level=lev,blocks=blocks))
scalar=pvs.Calculator(registrationName='Vorticity_H_over_Ulid',Input=reader);scalar.AttributeType='Cell Data';scalar.ResultArrayName='omega_H_U';scalar.Function='Vorticity*20';scalar.UpdatePipeline()
view=pvs.CreateView('RenderView');view.ViewSize=[1600,1800];view.UseColorPaletteForBackground=0;view.Background=[1,1,1];view.OrientationAxesVisibility=0
view.CameraParallelProjection=1;view.CameraPosition=[.5,.5,3];view.CameraFocalPoint=[.5,.5,0];view.CameraViewUp=[0,1,0];view.CameraParallelScale=.69
view.ViewTime=1000
show=pvs.Show(scalar,view);show.Representation='Surface';pvs.ColorBy(show,('CELLS','omega_H_U'))
lut=pvs.GetColorTransferFunction('omega_H_U');lut.RGBPoints=[0,.12,.16,.50,4,0,.65,.85,8,.15,.8,.4,12,.95,.95,.15,16,1,.45,0,20,.65,0,.05];lut.ColorSpace='RGB';lut.RescaleTransferFunction(0,20)
show.SetScalarBarVisibility(view,True)
bar=pvs.GetScalarBar(lut,view);bar.Title='|omega| H / U_lid';bar.ComponentTitle='';bar.Orientation='Horizontal';bar.WindowLocation='Any Location';bar.Position=[.20,.07];bar.ScalarBarLength=.60;bar.TitleColor=[0,0,0];bar.LabelColor=[0,0,0];bar.TitleFontSize=21;bar.LabelFontSize=19;bar.UseCustomLabels=1;bar.CustomLabels=[0,4,8,12,16,20];bar.AutomaticLabelFormat=1;bar.AddRangeLabels=0
colors=[[.08,.08,.08],[.85,.1,.12],[0,.45,.12]]
for lev,color in enumerate(colors):
    source=pvs.XMLPolyDataReader(registrationName=f'AMR_level_{lev}_box_bounds',FileName=[str(OUT/f'level{lev}_boxes.vtp')]);d=pvs.Show(source,view);d.Representation='Surface';d.ColorArrayName=['POINTS',''];d.DiffuseColor=color;d.AmbientColor=color;d.LineWidth=2.0 if lev else 2.5

def text(name,value,position,size,color=[0,0,0]):
    source=pvs.Text(registrationName=name);source.Text=value;d=pvs.Show(source,view);d.WindowLocation='Any Location';d.Position=position;d.FontSize=size;d.Color=color
text('figure_header','D2Q9 cavity, Re=1000, t=1000\nBlock outlines: L0 black / L1 red / L2 green',[.17,.905],22)
text('x_label','x / H',[.48,.115],20)
text('y_label','y / H',[.015,.52],20)
text('provenance','Native cell vorticity; H=1, U_lid=0.05; scale x20; colors clipped above 20\nstep 512000, levels 0-2; no smoothing; source: job626034 / D2Q9_amr_t1000',[.06,.015],12)
pvs.Render(view)
pvs.SaveScreenshot(str(OUT/'D2Q9_t1000_vorticity_amr.png'),view,ImageResolution=[1600,1800],FontScaling='Do not scale fonts')
pvs.SaveState(str(OUT/'D2Q9_t1000_vorticity_amr.pvsm'))
metadata=dict(host=socket.gethostname(),paraview=str(pvs.GetParaViewVersion()),input=str(SOURCE),header_sha256=hashlib.sha256((SOURCE/'Header').read_bytes()).hexdigest(),time=1000,step=512000,dimensions=2,levels=records,native_vorticity_range=[min(v[0] for v in ranges),max(v[1] for v in ranges)],definition='solver compute_vorticity magnitude; 2D abs(dv/dx-du/dy); cell associated; no smoothing',normalization='H/U_lid=20, H=1 U_lid=0.05',color_range=[0,20],outline='four bounds edges per actual VTK AMR block, distinct fixed level colors; static t1000',output=str(OUT/'D2Q9_t1000_vorticity_amr.png'))
(OUT/'render_metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(dict(output=metadata['output'],levels=[len(r['blocks']) for r in records],range=metadata['native_vorticity_range']),indent=2))
