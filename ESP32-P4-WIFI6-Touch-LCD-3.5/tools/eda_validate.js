const pages=await eda.dmt_Schematic.getAllSchematicPagesInfo();
const counts=[];
for(const page of pages){
  await eda.dmt_EditorControl.openDocument(page.uuid);
  const components=await eda.sch_PrimitiveComponent.getAll();
  let pins=0;
  for(const c of components)pins+=(await eda.sch_PrimitiveComponent.getAllPinsByPrimitiveId(c.getState_PrimitiveId())).length;
  counts.push({name:page.name,uuid:page.uuid,components:components.length,pins});
}
const netlist=await eda.sch_Netlist.getNetlist('Protel2');
const drc=await eda.sch_Drc.check(true,false,true);
return {pages:counts,netlist,drc};
