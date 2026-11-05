const pages=await eda.dmt_Schematic.getAllSchematicPagesInfo();
const counts=[];
for(const page of pages){
  await eda.dmt_EditorControl.openDocument(page.uuid);
  const components=await eda.sch_PrimitiveComponent.getAll();
  counts.push({title:page.title,uuid:page.uuid,components:components.length});
}
const netlist=await eda.sch_Netlist.getNetlist();
return {pages:counts,netlist};
