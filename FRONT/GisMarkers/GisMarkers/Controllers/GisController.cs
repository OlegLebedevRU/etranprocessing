using System;
using System.Collections.Generic;
using System.Linq;
using System.Web;
using System.Web.Mvc;
using GisMarkers.Models;
using log4net;

namespace GisMarkers.Controllers
{
    public class MyModel
    {
        public int SelectedId { get; set; }
        public string SelectedValue { get; set; }

        public IEnumerable<tb_Locations> Locations { get; set; }

        public IEnumerable<ts_LocationTypes> LocationTypes { get; set; }
    }

    public class GisController : Controller
    {
        private static readonly ILog Logger = LogManager.GetLogger(typeof(GisController));

        readonly EBSEntities _db = new EBSEntities();

        public ActionResult Index()
        {
            Logger.Info("ActionResult Index()");
            var myModel = new MyModel() { Locations = _db.tb_Locations, LocationTypes = _db.ts_LocationTypes};
            return View(myModel);
        }
        
        public ActionResult NewMarker(string typeId, string name, string description, string x, string y)
        {
            try
            {
                Logger.Info("ActionResult NewMarker() ... name " + name);
                _db.tb_Locations.Add(new tb_Locations()
                {
                    LocationTypeId = Convert.ToInt32(typeId),
                    Address = name,
                    Description = description,
                    CoordX = x,
                    CoordY = y
                });
                _db.SaveChanges();

                Logger.Info("ActionResult NewMarker() OK name " + name);
                //return Json(new { foo = "typeId=" + typeId + ";name=" + name + ";description=" + description+";x="+x+";y="+y });
                return Json(new { result = "OK" });

            }
            catch (Exception ex)
            {
                Logger.Error(ex);
            }
            return Json(new { result = "ERROR" });
        }
    }
}
