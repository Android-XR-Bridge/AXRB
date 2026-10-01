// Electron-updater owns version comparison, checksum/signature verification and
// NSIS installation. Keep prompts and AXRB shutdown separate for testing.
export class LauncherUpdates {
  constructor({updater, prompt, install, progress=()=>{}, log=()=>{}, canInstall=()=>true}) {
    Object.assign(this,{updater,prompt,install,progress,log,canInstall});
    this.pending=false; this.ready=false; this.started=false;
    updater.autoDownload=false;
    updater.autoInstallOnAppQuit=false;
    updater.allowPrerelease=false;
    updater.allowDowngrade=false;
    updater.on('error',error=>log(`Update: ${error.message}`));
    updater.on('download-progress',value=>progress(Math.min(1,Math.max(0,value.percent/100))));
    updater.on('update-available',info=>{void this.offer(info);});
  }
  async check() {
    if(this.started)return;
    this.started=true;
    try {await this.updater.checkForUpdates();} catch(error){this.log(`Update check: ${error.message}`);}
  }
  async offer(info) {
    if(this.pending)return;
    this.pending=true;
    try {
      const choice=await this.prompt({type:'question',title:'AXRB update',message:`AXRB ${info.version} is available.`,detail:'Download and install the update? AXRB will restart when installation finishes.',buttons:['Download and install','Later'],defaultId:0,cancelId:1,noLink:true});
      if(choice.response!==0)return;
      this.progress(0);
      await this.updater.downloadUpdate();
      this.ready=true;this.progress(-1);
      if(!this.canInstall()) {
        await this.prompt({type:'info',title:'AXRB update ready',message:'The update is downloaded.',detail:'Finish the current operation, then restart AXRB to be offered the cached update.',buttons:['OK']});return;
      }
      await this.install();
    } catch(error) {
      this.progress(-1);this.log(`Update failed: ${error.message}`);
      await this.prompt({type:'error',title:'AXRB update failed',message:'AXRB could not install the update.',detail:String(error.message).slice(0,1200),buttons:['OK']});
    } finally {this.pending=false;}
  }
  finishInstall() {if(!this.ready)throw new Error('No verified update is ready.');this.updater.quitAndInstall(false,true);}
}
