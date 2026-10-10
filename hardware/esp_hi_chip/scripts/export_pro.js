// Run with LCEDA Pro invoke --ext-uuid eda --code-file this-file.
// The active document must be the associated Rev A PCB. No order is placed.
const root = 'C:/Users/PC/Documents/LCEDA-Pro/projects/ESP-HI-C3-RevA/exports/';
const errors = await eda.pcb_Drc.check(true, false, true);
if (errors.length) throw new Error('Native DRC has unresolved errors; export blocked.');
const result = {nativeDrc: errors, exports: []};
async function save(file, name) {
    if (!file) throw new Error('No export produced: ' + name);
    const ok = await eda.sys_FileSystem.saveFileToFileSystem(root + name, file, undefined, true);
    if (!ok) throw new Error('Export write failed: ' + name);
    result.exports.push({name, bytes: file.size});
}
await save(await eda.pcb_ManufactureData.getGerberFile('ESP-HI-C3-RevA-prototype'), 'ESP-HI-C3-RevA-prototype-gerber.zip');
await save(await eda.pcb_ManufactureData.getPickAndPlaceFile('ESP-HI-C3-RevA-placement-all', 'csv', 'mm'), 'ESP-HI-C3-RevA-placement-all.csv');
await save(await eda.pcb_ManufactureData.getInteractiveBomFile('ESP-HI-C3-RevA-interactive-BOM'), 'ESP-HI-C3-RevA-interactive-BOM.html');
await eda.pcb_Document.save();
return result;
