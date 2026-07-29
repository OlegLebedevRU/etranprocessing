using System;
using System.Collections.Generic;
using System.Data.Entity;
using System.Linq;
using System.Security.Policy;
using System.Web;

namespace GisMarkers.Models
{
    public class Marker
    {
        public int Id { get; set; }
        public string Name { get; set; }

    }


    public class DbMarkers : DbContext
    {

        public DbMarkers()
            : base("name=EBSEntities")
        {

        }

        public DbSet<ymapapiv2_markers_cat> ymapapiv2_markers_cats { get; set; }

    }
}