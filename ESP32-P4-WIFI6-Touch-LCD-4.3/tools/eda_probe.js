const pages=await eda.dmt_Schematic.getAllSchematicPagesInfo();
const tab=await eda.dmt_EditorControl.openDocument(pages[0].uuid);
let first=null,pins=null,protel=null,error=null;
try{
const components=await eda.sch_PrimitiveComponent.getAll();
first=components[0];
pins=await eda.sch_PrimitiveComponent.getAllPinsByPrimitiveId(first.getState_PrimitiveId());
protel=await eda.sch_Netlist.getNetlist('Protel2');
}catch(e){error=String(e);}
await eda.dmt_EditorControl.zoomToAllPrimitives(tab);
const blob=await eda.dmt_EditorControl.getCurrentRenderedAreaImage(tab);
let image=null;
if(blob)image=await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=reject;r.readAsDataURL(blob);});
return {pages,tab,first,pins,protel,image,error};
